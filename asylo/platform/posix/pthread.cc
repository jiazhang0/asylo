/*
 *
 * Copyright 2017 Asylo authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <pthread.h>

#include <signal.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <type_traits>

#include "absl/container/flat_hash_map.h"
#include "asylo/platform/arch/include/trusted/enclave_interface.h"
#include "asylo/platform/arch/include/trusted/host_calls.h"
#include "asylo/platform/common/time_util.h"
#include "asylo/platform/core/trusted_global_state.h"
#include "asylo/platform/posix/pthread_impl.h"
#include "asylo/platform/posix/threading/thread_manager.h"

namespace asylo {
namespace pthread_impl {

static inline int InterlockedExchange(pthread_spinlock_t *dest,
                                      pthread_spinlock_t old_value,
                                      pthread_spinlock_t new_value);

static inline int InterlockedExchange(pthread_spinlock_t *dest,
                                      pthread_spinlock_t old_value,
                                      pthread_spinlock_t new_value) {
  return __sync_val_compare_and_swap(dest, old_value, new_value);
}

// Confirms that |parameter| is not nullptr and is within the enclave.
template <typename T>
static int check_parameter(const T *parameter) {
  if (!parameter || !enc_is_within_enclave(parameter, sizeof(*parameter))) {
    return EINVAL;
  }
  return 0;
}

static thread_local absl::flat_hash_map<uint64_t, void *> *tls_map = nullptr;

static void init_tls_map() {
  tls_map = new absl::flat_hash_map<uint64_t, void *>();
}

inline int pthread_spin_lock(pthread_spinlock_t *lock) {
  while (InterlockedExchange(lock, 0, 1) != 0) {
    while (*lock) {
      enc_pause();
    }
  }
  return 0;
}

inline int pthread_spin_unlock(pthread_spinlock_t *lock) {
  *lock = 0;
  return 0;
}

// Provides RAII wrapper around pthread_spinlock_t.
class SpinLock {
 public:
  SpinLock(pthread_mutex_t *mutex) : SpinLock(&mutex->_lock) {}
  SpinLock(pthread_cond_t *cond) : SpinLock(&cond->_lock) {}
  SpinLock(pthread_spinlock_t *lock) {
    lock_ = lock;
    pthread_spin_lock(lock_);
  }

  ~SpinLock() { pthread_spin_unlock(lock_); }

 private:
  pthread_spinlock_t *lock_;
};

__pthread_list_node_t *alloc_list_node(pthread_t thread_id) {
  __pthread_list_node_t *node = new __pthread_list_node_t;
  node->_thread_id = thread_id;
  node->_next = nullptr;
  return node;
}

void free_list_node(__pthread_list_node_t *node) {
  delete node;
}

PthreadListWrapper::PthreadListWrapper(pthread_mutex_t *mutex)
    : list_(&mutex->_queue) {}

PthreadListWrapper::PthreadListWrapper(pthread_cond_t *condvar)
    : list_(&condvar->_queue) {}

PthreadListWrapper::PthreadListWrapper(__pthread_list_t *list,
                                       const std::function<void()> &abort_func)
    : list_(list), abort_func_(abort_func) {
  if (list_ == nullptr) {
    abort_func_();
  }
}

void PthreadListWrapper::Pop() {
  if (list_->_first == nullptr) {
    return abort_func_();
  }

  __pthread_list_node_t *old_first = list_->_first;
  list_->_first = old_first->_next;
  free_list_node(old_first);
}

pthread_t PthreadListWrapper::Front() const {
  if (list_->_first == nullptr) {
    return PTHREAD_T_NULL;
  }
  return list_->_first->_thread_id;
}

void PthreadListWrapper::Push(const pthread_t id) {
  __pthread_list_node_t *last = alloc_list_node(id);

  if (!list_->_first) {
    list_->_first = last;
    return;
  }

  __pthread_list_node_t *current = list_->_first;
  while (current->_next) {
    current = current->_next;
  }
  current->_next = last;
}

bool PthreadListWrapper::Remove(const pthread_t id) {
  __pthread_list_node_t *curr, *prev;

  for (curr = list_->_first, prev = nullptr; curr != nullptr;
       prev = curr, curr = curr->_next) {
    if (curr->_thread_id == id) {
      if (prev == nullptr) {
        // Node to remove was the first item in the list. Change the list head.
        list_->_first = curr->_next;
      } else {
        // Set previous node's next to be the deleted node's next.
        prev->_next = curr->_next;
      }

      free_list_node(curr);
      return true;
    }
  }

  return false;
}

bool PthreadListWrapper::Contains(const pthread_t id) const {
  __pthread_list_node_t *current = list_->_first;
  while (current) {
    if (current->_thread_id == id) {
      return true;
    }
    current = current->_next;
  }
  return false;
}

void PthreadListWrapper::Drain() {
  while (!Empty()) {
    Pop();
  }
}

bool PthreadListWrapper::Empty() const {
  const __pthread_list_node_t *current = list_->_first;
  return current == PTHREAD_T_NULL;
}

int pthread_mutex_check_parameter(pthread_mutex_t *mutex) {
  int ret = check_parameter<pthread_mutex_t>(mutex);
  if (ret != 0) {
    return ret;
  }

  if (mutex->_control != PTHREAD_MUTEX_RECURSIVE &&
      mutex->_control != PTHREAD_MUTEX_NONRECURSIVE) {
    return EINVAL;
  }
  return 0;
}

// Returns locks the mutex and returns 0 if possible. Returns EBUSY if the mutex
// is taken. |mutex|->_lock must be locked.
int pthread_mutex_lock_internal(pthread_mutex_t *mutex) {
  const pthread_t self = pthread_self();

  if (mutex->_control == PTHREAD_MUTEX_RECURSIVE && mutex->_owner == self) {
    mutex->_refcount++;
    return 0;
  }

  PthreadListWrapper list(mutex);
  const pthread_t first_waiter = list.Front();
  if (mutex->_owner == PTHREAD_T_NULL &&
      (first_waiter == self || first_waiter == PTHREAD_T_NULL)) {
    if (first_waiter == self) {
      list.Pop();
    }

    mutex->_owner = self;
    mutex->_refcount++;
    return 0;
  }

  return EBUSY;
}

}  //  namespace pthread_impl
}  //  namespace asylo

using asylo::ThreadManager;
using asylo::pthread_impl::check_parameter;
using asylo::pthread_impl::init_tls_map;
using asylo::pthread_impl::pthread_mutex_check_parameter;
using asylo::pthread_impl::pthread_mutex_lock_internal;
using asylo::pthread_impl::PthreadListWrapper;
using asylo::pthread_impl::SpinLock;
using asylo::pthread_impl::tls_map;

extern "C" {

// Functions available via <pthread.h>

pthread_t pthread_self() {
  static_assert(sizeof(pthread_t) == sizeof(uint64_t) &&
                    (std::is_pointer<pthread_t>::value ||
                     std::is_integral<pthread_t>::value),
                "pthread_t must be a 64-bit integer or pointer type.");
  return static_cast<pthread_t>(enc_thread_self());
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg) {
  std::function<void *(void *)> start_function(start_routine);

  ThreadManager *const thread_manager = ThreadManager::GetInstance();
  return thread_manager->CreateThread(start_function, arg, thread);
}

int pthread_join(pthread_t thread, void **value_ptr) {
  ThreadManager *const thread_manager = ThreadManager::GetInstance();
  return thread_manager->JoinThread(thread, value_ptr);
}

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) {
  static pthread_key_t next_key = 0;
  static pthread_mutex_t next_key_lock = PTHREAD_MUTEX_INITIALIZER;

  if (pthread_mutex_lock(&next_key_lock) != 0) {
    abort();
  }
  *key = next_key++;
  if (pthread_mutex_unlock(&next_key_lock) != 0) {
    abort();
  }
  return 0;
}

int pthread_key_delete(pthread_key_t key) { return 0; }

void *pthread_getspecific(pthread_key_t key) {
  if (!tls_map) {
    init_tls_map();
  }

  absl::flat_hash_map<uint64_t, void *>::const_iterator specific =
      tls_map->find(key);
  if (specific == tls_map->end()) {
    return nullptr;
  }

  return specific->second;
}

int pthread_setspecific(pthread_key_t key, const void *value) {
  if (!tls_map) {
    init_tls_map();
  }

  tls_map->emplace(key, const_cast<void *>(value));
  return 0;
}
// Initializes |mutex|, |attr| is unused.
int pthread_mutex_init(pthread_mutex_t *mutex,
                       const pthread_mutexattr_t *attr) {
  int ret = check_parameter<pthread_mutex_t>(mutex);
  if (ret != 0) {
    return ret;
  }

  *mutex = PTHREAD_MUTEX_INITIALIZER;
  return 0;
}

// Destroys |mutex|, returns error if there are threads waiting on |mutex|.
int pthread_mutex_destroy(pthread_mutex_t *mutex) {
  int ret = check_parameter<pthread_mutex_t>(mutex);
  if (ret != 0) {
    return ret;
  }

  SpinLock lock(mutex);
  PthreadListWrapper list(mutex);
  if (!list.Empty()) {
    return EBUSY;
  }

  return 0;
}

// Locks |mutex|.
int pthread_mutex_lock(pthread_mutex_t *mutex) {
  int ret = pthread_mutex_check_parameter(mutex);
  if (ret != 0) {
    return ret;
  }

  PthreadListWrapper list(mutex);
  {
    SpinLock lock(mutex);
    list.Push(pthread_self());
  }

  while (true) {
    {
      SpinLock lock(mutex);
      ret = pthread_mutex_lock_internal(mutex);
    }
    if (ret == 0) {
      return ret;
    }

    enc_pause();
  }
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
  int ret = pthread_mutex_check_parameter(mutex);
  if (ret != 0) {
    return ret;
  }

  SpinLock lock(mutex);
  ret = pthread_mutex_lock_internal(mutex);

  return ret;
}

// Unlocks |mutex|.
int pthread_mutex_unlock(pthread_mutex_t *mutex) {
  int ret = pthread_mutex_check_parameter(mutex);
  if (ret != 0) {
    return ret;
  }

  const pthread_t self = pthread_self();

  SpinLock lock(mutex);

  if (mutex->_owner == PTHREAD_T_NULL) {
    return EINVAL;
  }

  if (mutex->_owner != self) {
    return EPERM;
  }

  mutex->_refcount--;
  if (mutex->_refcount == 0) {
    mutex->_owner = PTHREAD_T_NULL;
  }

  return 0;
}

// Runs the given |init_routine| exactly once.
int pthread_once(pthread_once_t *once, void (*init_routine)(void)) {
  int ret = pthread_mutex_lock(&once->_mutex);
  if (ret != 0) {
    return ret;
  }

  if (!once->_ran) {
    init_routine();
    once->_ran = true;
  }

  return pthread_mutex_unlock(&once->_mutex);
}

// Initializes |cond|, |attr| is unused.
int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
  int ret = check_parameter<pthread_cond_t>(cond);
  if (ret != 0) {
    return ret;
  }

  *cond = PTHREAD_COND_INITIALIZER;
  return 0;
}

// Destroys |cond|, errors if there are threads waiting on |cond|.
int pthread_cond_destroy(pthread_cond_t *cond) {
  int ret = check_parameter<pthread_cond_t>(cond);
  if (ret != 0) {
    return ret;
  }

  SpinLock lock(cond);
  PthreadListWrapper list(cond);
  if (!list.Empty()) {
    return EBUSY;
  }

  return 0;
}

// Blocks until the given |cond| is signaled or broadcasted, or a timeout
// occurs. |mutex| must be locked before calling and will be locked on return.
// Returns ETIMEDOUT if |deadline| (which is an absolute time) is not null, the
// current time is later than |deadline|, and |cond| has not yet been signaled
// or broadcasted.
//
// Warning: Enclaves do not currently have a source of secure time. A hostile
// host could cause this function to either return ETIMEDOUT immediately or
// never time out, acting like pthread_cond_wait().
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *deadline) {
  int ret = check_parameter<pthread_cond_t>(cond);
  if (ret != 0) {
    return ret;
  }

  ret = check_parameter<pthread_mutex_t>(mutex);
  if (ret != 0) {
    return ret;
  }

  // If a deadline has been specified, ensure it is valid.
  if (deadline != nullptr) {
    ret = check_parameter<timespec>(deadline);
    if (ret != 0) {
      return ret;
    }
  }

  const pthread_t self = pthread_self();

  PthreadListWrapper list(cond);
  {
    SpinLock lock(cond);
    list.Push(self);
  }

  ret = pthread_mutex_unlock(mutex);
  if (ret != 0) {
    return ret;
  }

  while (true) {
    enc_pause();

    // If a deadline has been specified, check to see if it has passed.
    if (deadline != nullptr) {
      timespec curr_time;
      ret = clock_gettime(CLOCK_REALTIME, &curr_time);
      if (ret != 0) {
        break;
      }

      // TimeSpecSubtract returns true if deadline < curr_time.
      timespec time_left;
      if (asylo::TimeSpecSubtract(*deadline, curr_time, &time_left)) {
        ret = ETIMEDOUT;
        break;
      }
    }

    SpinLock lock(cond);
    if (!list.Contains(self)) {
      break;
    }
  }
  {
    SpinLock lock(cond);
    list.Remove(self);
  }

  // Only set the retval to be the result of re-locking the mutex if there isn't
  // already another error we're trying to return. Otherwise, we give preference
  // to returning the pre-existing error and drop the error caused by re-locking
  // the mutex.
  int relock_ret = pthread_mutex_lock(mutex);
  if (ret != 0) {
    return ret;
  }
  return relock_ret;
}

// Blocks until the given |cond| is signaled or broadcasted. |mutex| must  be
// locked before calling and will be locked on return.
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
  return pthread_cond_timedwait(cond, mutex, nullptr);
}

int pthread_condattr_init(pthread_condattr_t *attr) { return 0; }

int pthread_condattr_destroy(pthread_condattr_t *attr) { return 0; }

// Wakes the first waiting thread on |cond|.
int pthread_cond_signal(pthread_cond_t *cond) {
  int ret = check_parameter<pthread_cond_t>(cond);
  if (ret != 0) {
    return ret;
  }

  SpinLock lock(cond);
  PthreadListWrapper list(cond);
  if (list.Empty()) {
    return 0;
  }

  list.Pop();

  return 0;
}

// Wakes all the waiting threads on |cond|.
int pthread_cond_broadcast(pthread_cond_t *cond) {
  int ret = check_parameter<pthread_cond_t>(cond);
  if (ret != 0) {
    return ret;
  }

  PthreadListWrapper list(cond);
  {
    SpinLock lock(cond);
    list.Drain();
  }

  return 0;
}

int pthread_equal(pthread_t thread_one, pthread_t thread_two) {
  if (thread_one == thread_two) {
    return -1;
  }
  return 0;
}

int pthread_mutexattr_init(pthread_mutexattr_t *mutexattr) { return 0; }
int pthread_mutexattr_destroy(pthread_mutexattr_t *mutexattr) { return 0; }
int pthread_mutexattr_settype(pthread_mutexattr_t *mutexattr, int type) {
  return 0;
}

int pthread_attr_init(pthread_attr_t *attr) { return 0; }
int pthread_attr_destroy(pthread_attr_t *attr) { return 0; }
int pthread_attr_setdetachstate(pthread_attr_t *attr, int type) { return 0; }
int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize) {
  return ENOSYS;
}
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize) {
  return ENOSYS;
}

int pthread_cancel(pthread_t unused) { return ENOSYS; }
int pthread_setcancelstate(int state, int *oldstate) { return ENOSYS; }
int pthread_setcanceltype(int type, int *oldtype) { return ENOSYS; }

}  // extern "C"
