/*
 *
 * Copyright 2018 Asylo authors
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

#ifndef ASYLO_CRYPTO_AEAD_KEY_H_
#define ASYLO_CRYPTO_AEAD_KEY_H_

#include <vector>

#include "asylo/crypto/algorithms.pb.h"  // IWYU pragma: export
#include "asylo/crypto/util/byte_container_view.h"
#include "asylo/util/cleansing_types.h"
#include "asylo/util/status.h"

namespace asylo {

// Abstract class representing a key used for AEAD (Authenticated Encryption
// with Associated Data) operations.
class AeadKey {
 public:
  AeadKey() = default;
  virtual ~AeadKey() = default;

  // Gets the AEAD scheme used by this AeadKey.
  virtual AeadScheme GetAeadScheme() const = 0;

  // Gets the nonce size in bytes expected for the Seal() and Open() operations.
  virtual size_t NonceSize() const = 0;

  // Implements the AEAD Seal operation. |nonce|.size() must be same as the
  // value returned by NonceSize(). This method is marked non-const to allow
  // for implementations that internally manage key rotation.
  virtual Status Seal(ByteContainerView plaintext,
                      ByteContainerView associated_data,
                      ByteContainerView nonce,
                      std::vector<uint8_t> *ciphertext) = 0;

  // Implements the AEAD Open operation. |nonce|.size() must be same as the
  // value returned by NonceSize().
  virtual Status Open(ByteContainerView ciphertext,
                      ByteContainerView associated_data,
                      ByteContainerView nonce,
                      CleansingVector<uint8_t> *plaintext) const = 0;
};

}  // namespace asylo

#endif  // ASYLO_CRYPTO_AEAD_KEY_H_
