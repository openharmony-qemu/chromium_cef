/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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
 */

#include "libcef/browser/alloy/alloy_client_cert_lookup_table.h"

#include "base/logging.h"
#include "base/no_destructor.h"

#if BUILDFLAG(IS_ARKWEB)
// static
std::map<std::string, AlloyCert>& AlloyClientCertLookupTable::GetCerts() {
  static base::NoDestructor<std::map<std::string, AlloyCert>> certs;
  return *certs;
}

// static
std::set<std::string>& AlloyClientCertLookupTable::GetDenieds() {
  static base::NoDestructor<std::set<std::string>> denieds;
  return *denieds;
}

void AlloyClientCertLookupTable::Clear() {
  GetCerts().clear();
  GetDenieds().clear();
}

bool AlloyClientCertLookupTable::IsDenied(const std::string& host, int port) {
  std::string host_port = HostAndPort(host, port);
  auto it = GetDenieds().find(host_port);
  if (it != GetDenieds().end()) {
    return true;
  }
  return false;
}

void AlloyClientCertLookupTable::Allow(const std::string& host,
                                       int port,
                                       const std::string& private_key,
                                       const std::string& cert_chain) {
  std::string host_port = HostAndPort(host, port);
  AlloyCert cert(private_key, cert_chain);
  GetCerts()[host_port] = cert;
  GetDenieds().erase(host_port);
}

void AlloyClientCertLookupTable::Deny(const std::string& host, int port) {
  std::string host_port = HostAndPort(host, port);
  GetCerts().erase(host_port);
  GetDenieds().insert(host_port);
}

bool AlloyClientCertLookupTable::GetCertData(const std::string& host,
                                             int port,
                                             AlloyCert& cert) {
  std::string host_port = HostAndPort(host, port);
  auto it = GetCerts().find(host_port);
  if (it != GetCerts().end()) {
    cert = (*it).second;
    return true;
  }
  return false;
}

std::string AlloyClientCertLookupTable::HostAndPort(const std::string& host,
                                                    int port) {
  return host + ":" + std::to_string(port);
}

#endif  // IS_ARKWEB
