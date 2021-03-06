/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define _DEFAULT_SOURCE
#include "os.h"
#include "taosdef.h"
#include "tmodule.h"
#include "tsched.h"
#include "mnode.h"
#include "taccount.h"
#include "tbalance.h"
#include "tcluster.h"
#include "tgrant.h"
#include "mpeer.h"
#include "mgmtDb.h"
#include "mgmtDClient.h"
#include "mgmtDServer.h"
#include "mgmtSdb.h"
#include "mgmtVgroup.h"
#include "mgmtUser.h"
#include "mgmtTable.h"
#include "mgmtShell.h"

static int32_t mgmtCheckMgmtRunning();
void *tsMgmtTmr = NULL;

int32_t mgmtInitSystem() {
  if (mgmtInitShell() != 0) {
    mError("failed to init shell");
    return -1;
  }

  struct stat dirstat;
  bool fileExist  = (stat(tsMnodeDir, &dirstat) == 0);
  bool asMaster = (strcmp(tsMasterIp, tsPrivateIp) == 0);

  if (asMaster || fileExist) {
    if (mgmtStartSystem() != 0) {
      return -1;
    }
  }

  return 0;
}

int32_t mgmtStartSystem() {
  mPrint("starting to initialize TDengine mgmt ...");

  struct stat dirstat;
  if (stat(tsMnodeDir, &dirstat) < 0) {
    mkdir(tsMnodeDir, 0755);
  }

  if (mgmtCheckMgmtRunning() != 0) {
    mPrint("TDengine mgmt module already started...");
    return 0;
  }

  tsMgmtTmr = taosTmrInit((tsMaxShellConns) * 3, 200, 3600000, "MND");
  if (tsMgmtTmr == NULL) {
    mError("failed to init timer");
    return -1;
  }

  if (acctInit() < 0) {
    mError("failed to init accts");
    return -1;
  }

  if (grantInit() < 0) {
    mError("failed to init grants");
    return -1;
  }

  if (mgmtInitUsers() < 0) {
    mError("failed to init users");
    return -1;
  }

  if (clusterInit() < 0) {
    mError("failed to init dnodes");
    return -1;
  }

  if (mgmtInitDbs() < 0) {
    mError("failed to init dbs");
    return -1;
  }

  if (mgmtInitVgroups() < 0) {
    mError("failed to init vgroups");
    return -1;
  }

  if (mgmtInitTables() < 0) {
    mError("failed to init tables");
    return -1;
  }

  if (mpeerInit() < 0) {
    mError("failed to init mpeers");
    return -1;
  }

  if (sdbInit() < 0) {
    mError("failed to init sdb");
    return -1;
  }

  if (mgmtInitDClient() < 0) {
    return -1;
  }

  if (mgmtInitDServer() < 0) {
    return -1;
  }

  if (balanceInit() < 0) {
    mError("failed to init dnode balance")
  }

  grantReset(TSDB_GRANT_ALL, 0);

  mPrint("TDengine mgmt is initialized successfully");

  return 0;
}


void mgmtStopSystem() {
  if (mpeerIsMaster()) {
    mTrace("it is a master mgmt node, it could not be stopped");
    return;
  }

  mgmtCleanUpSystem();
  remove(tsMnodeDir);
}

void mgmtCleanUpSystem() {
  mPrint("starting to clean up mgmt");
  grantCleanUp();
  mpeerCleanup();
  balanceCleanUp();
  mgmtCleanUpShell();
  mgmtCleanupDClient();
  mgmtCleanupDServer();
  mgmtCleanUpTables();
  mgmtCleanUpVgroups();
  mgmtCleanUpDbs();
  clusterCleanUp();
  mgmtCleanUpUsers();
  acctCleanUp();
  sdbCleanUp();
  taosTmrCleanUp(tsMgmtTmr);
  mPrint("mgmt is cleaned up");
}

static int32_t mgmtCheckMgmtRunning() {
  if (tsModuleStatus & (1 << TSDB_MOD_MGMT)) {
    return -1;
  }

  tsetModuleStatus(TSDB_MOD_MGMT);
  return 0;
}