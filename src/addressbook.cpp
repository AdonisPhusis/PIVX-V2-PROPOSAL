// Copyright (c) 2019-2021 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "addressbook.h"

namespace AddressBook {

    namespace AddressBookPurpose {
        const std::string UNKNOWN{"unknown"};
        const std::string RECEIVE{"receive"};
        const std::string SEND{"send"};
        const std::string SHIELDED_RECEIVE{"shielded_receive"};
        const std::string SHIELDED_SEND{"shielded_spend"};
        const std::string EXCHANGE_ADDRESS{"exchange_address"};
    }

    bool IsShieldedPurpose(const std::string& purpose) {
        return purpose == AddressBookPurpose::SHIELDED_RECEIVE
               || purpose == AddressBookPurpose::SHIELDED_SEND;
    }

    bool IsExchangePurpose(const std::string& purpose)  {
        return purpose == AddressBookPurpose::EXCHANGE_ADDRESS;
    }

    bool CAddressBookData::isSendPurpose() const {
        return purpose == AddressBookPurpose::SEND;
    }

    bool CAddressBookData::isReceivePurpose() const {
        return purpose == AddressBookPurpose::RECEIVE;
    }

    bool CAddressBookData::isShieldedReceivePurpose() const {
        return purpose == AddressBookPurpose::SHIELDED_RECEIVE;
    }

    bool CAddressBookData::isShielded() const {
        return IsShieldedPurpose(purpose);
    }


}

