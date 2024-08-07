/*
* This file was generated by the CommonAPI Generators.
* Used org.genivi.commonapi.dbus 3.2.0.v202012010857.
* Used org.franca.core 0.13.1.201807231814.
*
* This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
* If a copy of the MPL was not distributed with this file, You can obtain one at
* http://mozilla.org/MPL/2.0/.
*/
#include <v1/org/genivi/mediamanager/Indexer.hpp>
#include <v1/org/genivi/mediamanager/IndexerDBusStubAdapter.hpp>

namespace v1 {
namespace org {
namespace genivi {
namespace mediamanager {

std::shared_ptr<CommonAPI::DBus::DBusStubAdapter> createIndexerDBusStubAdapter(
                   const CommonAPI::DBus::DBusAddress &_address,
                   const std::shared_ptr<CommonAPI::DBus::DBusProxyConnection> &_connection,
                   const std::shared_ptr<CommonAPI::StubBase> &_stub) {
    return std::make_shared< IndexerDBusStubAdapter<::v1::org::genivi::mediamanager::IndexerStub>>(_address, _connection, std::dynamic_pointer_cast<::v1::org::genivi::mediamanager::IndexerStub>(_stub));
}

void initializeIndexerDBusStubAdapter() {
    CommonAPI::DBus::Factory::get()->registerStubAdapterCreateMethod(
        Indexer::getInterface(), &createIndexerDBusStubAdapter);
}

INITIALIZER(registerIndexerDBusStubAdapter) {
    CommonAPI::DBus::Factory::get()->registerInterface(initializeIndexerDBusStubAdapter);
}

} // namespace mediamanager
} // namespace genivi
} // namespace org
} // namespace v1
