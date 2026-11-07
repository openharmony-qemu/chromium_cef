/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2018-2022. All rights reserved.
 */

#include "components/password_manager/core/browser/password_store/password_store_backend.h"

#include "chrome/browser/browser_process.h"
#include "components/password_manager/core/browser/password_store/login_database.h"
#include "components/password_manager/core/browser/password_store/password_store_built_in_backend.h"
#include "components/password_manager/core/browser/password_store_factory_util.h"
#include "components/prefs/pref_service.h"

namespace password_manager {

std::unique_ptr<PasswordStoreBackend> PasswordStoreBackend::Create(
    const base::FilePath& login_db_path,
    PrefService* prefs) {
  return std::make_unique<PasswordStoreBuiltInBackend>(
      CreateLoginDatabase(password_manager::kProfileStore, login_db_path, prefs),
      syncer::WipeModelUponSyncDisabledBehavior::kNever, prefs, g_browser_process->os_crypt_async());
}

}  // namespace password_manager
