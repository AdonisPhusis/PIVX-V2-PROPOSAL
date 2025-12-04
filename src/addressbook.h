// Copyright (c) 2019-2020 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HU_ADDRESSBOOK_H
#define HU_ADDRESSBOOK_H

#include <map>
#include <string>

namespace AddressBook {

    namespace AddressBookPurpose {
        extern const std::string UNKNOWN;
        extern const std::string RECEIVE;
        extern const std::string SEND;
        extern const std::string SHIELDED_RECEIVE;
        extern const std::string SHIELDED_SEND;
        extern const std::string EXCHANGE_ADDRESS;
    }

    bool IsShieldedPurpose(const std::string& purpose);
    bool IsExchangePurpose(const std::string& purpose);

/** Address book data */
    class CAddressBookData {
    public:

        std::string name;
        std::string purpose;

        CAddressBookData() {
            purpose = AddressBook::AddressBookPurpose::UNKNOWN;
        }

        typedef std::map<std::string, std::string> StringMap;
        StringMap destdata;

        bool isSendPurpose() const;
        bool isReceivePurpose() const;
        bool isShieldedReceivePurpose() const;
        bool isShielded() const;
    };

}

#endif // HU_ADDRESSBOOK_H
