////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2016 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Jan Steemann
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGOD_UTILS_EXPLICIT_TRANSACTION_H
#define ARANGOD_UTILS_EXPLICIT_TRANSACTION_H 1

#include "Basics/Common.h"

#include "StorageEngine/TransactionState.h"
#include "Transaction/Methods.h"
#include "Transaction/V8Context.h"

namespace arangodb {

class UserTransaction final : public transaction::Methods {
 public:
  /// @brief create the transaction
  UserTransaction(std::shared_ptr<transaction::V8Context> transactionContext,
                      std::vector<std::string> const& readCollections,
                      std::vector<std::string> const& writeCollections,
                      std::vector<std::string> const& exclusiveCollections,
                      double lockTimeout, bool waitForSync,
                      bool allowImplicitCollections)
      : transaction::Methods(transactionContext) {
    addHint(transaction::Hints::Hint::LOCK_ENTIRELY);

    for (auto const& it : exclusiveCollections) {
      addCollection(it, AccessMode::Type::EXCLUSIVE);
    }

    for (auto const& it : writeCollections) {
      addCollection(it, AccessMode::Type::WRITE);
    }

    for (auto const& it : readCollections) {
      addCollection(it, AccessMode::Type::READ);
    }

    _state->timeout(lockTimeout);
    _state->waitForSync(waitForSync);
    _state->allowImplicitCollections(allowImplicitCollections);
  }

};
}

#endif
