/****************************************************************************
 * include/nuttx/rptun/rptun.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __INCLUDE_NUTTX_RPTUN_RPTUN_H
#define __INCLUDE_NUTTX_RPTUN_RPTUN_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_RPTUN

#include <metal/cache.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <openamp/remoteproc.h>
#include <openamp/rpmsg_virtio.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define _RPTUNIOCVALID(c)     _RPMSGIOCVALID(c)
#define _RPTUNIOC(nr)         _RPMSGIOC(nr)

#define RPTUNIOC_START        _RPTUNIOC(100)
#define RPTUNIOC_STOP         _RPTUNIOC(101)
#define RPTUNIOC_RESET        _RPTUNIOC(102)

#define RPTUN_NOTIFY_ALL      (UINT32_MAX - 0)

#define RPTUN_CMD_DEFAULT     0x0
#define RPTUN_CMD_PANIC       0x1
#define RPTUN_CMD_STOP        0x2
#define RPTUN_CMD_READY       0x3
#define RPTUN_CMD_RESTART     0x4
#define RPTUN_CMD_MASK        0xffff
#define RPTUN_CMD_SHIFT       16

#define RPTUN_CMD(c,v)        (((c) << RPTUN_CMD_SHIFT) | ((v) & RPTUN_CMD_MASK))
#define RPTUN_GET_CMD(c)      ((c) >> RPTUN_CMD_SHIFT)
#define RPTUN_GET_CMD_VAL(c)  ((c) & RPTUN_CMD_MASK)

#define RPTUN_RSC2CMD(r)      \
  ((FAR struct rptun_cmd_s *)&((FAR struct resource_table *)(r))->reserved[0])

#ifdef CONFIG_OPENAMP_CACHE
#  define RPTUN_INVALIDATE(x) metal_cache_invalidate(&x, sizeof(x))
#else
#  define RPTUN_INVALIDATE(x)
#endif

/* Access macros ************************************************************/

/****************************************************************************
 * Name: RPTUN_GET_LOCAL_CPUNAME
 *
 * Description:
 *   Get local cpu name
 *
 * Input Parameters:
 *   dev  - Device-specific state data
 *
 * Returned Value:
 *   Cpu name on success, NULL on failure.
 *
 ****************************************************************************/

#define RPTUN_GET_LOCAL_CPUNAME(d) ((d)->ops->get_local_cpuname ? \
                                    (d)->ops->get_local_cpuname(d) : "")

/****************************************************************************
 * Name: RPTUN_GET_CPUNAME
 *
 * Description:
 *   Get remote cpu name
 *
 * Input Parameters:
 *   dev  - Device-specific state data
 *
 * Returned Value:
 *   Cpu name on success, NULL on failure.
 *
 ****************************************************************************/

#define RPTUN_GET_CPUNAME(d) ((d)->ops->get_cpuname ? \
                              (d)->ops->get_cpuname(d) : "")

/****************************************************************************
 * Name: RPTUN_GET_FIRMWARE
 *
 * Description:
 *   Get remote firmware name
 *
 * Input Parameters:
 *   dev  - Device-specific state data
 *
 * Returned Value:
 *   Firmware name on success, NULL on failure.
 *
 ****************************************************************************/

#define RPTUN_GET_FIRMWARE(d) ((d)->ops->get_firmware ? \
                               (d)->ops->get_firmware(d) : NULL)

/****************************************************************************
 * Name: RPTUN_GET_ADDRENV
 *
 * Description:
 *   Get address env list
 *
 * Input Parameters:
 *   dev  - Device-specific state data
 *
 * Returned Value:
 *   Addrenv pointer on success, NULL on failure.
 *
 ****************************************************************************/

#define RPTUN_GET_ADDRENV(d) ((d)->ops->get_addrenv ? \
                              (d)->ops->get_addrenv(d) : NULL)

/****************************************************************************
 * Name: RPTUN_GET_RESOURCE
 *
 * Description:
 *   Get rptun resource
 *
 * Input Parameters:
 *   dev  - Device-specific state data
 *
 * Returned Value:
 *   Resource pointer on success, NULL on failure
 *
 ****************************************************************************/

#define RPTUN_GET_RESOURCE(d) ((d)->ops->get_resource ? \
                               (d)->ops->get_resource(d) : NULL)

/****************************************************************************
 * Name: RPTUN_IS_AUTOSTART
 *
 * Description:
 *   AUTO start or not
 *
 * Input Parameters:
 *   dev  - Device-specific state data
 *
 * Returned Value:
 *   True autostart, false not autostart
 *
 ****************************************************************************/

#define RPTUN_IS_AUTOSTART(d) ((d)->ops->is_autostart ? \
                               (d)->ops->is_autostart(d) : false)

/****************************************************************************
 * Name: RPTUN_IS_MASTER
 *
 * Description:
 *   IS master or not
 *
 * Input Parameters:
 *   dev  - Device-specific state data
 *
 * Returned Value:
 *   True master, false remote
 *
 ****************************************************************************/

#define RPTUN_IS_MASTER(d) ((d)->ops->is_master ? \
                            (d)->ops->is_master(d) : false)

/****************************************************************************
 * Name: RPTUN_CONFIG
 *
 * Description:
 *   CONFIG remote cpu
 *
 * Input Parameters:
 *   dev  - Device-specific state data
 *   data - Device-specific private data
 *
 * Returned Value:
 *   OK unless an error occurs.  Then a negated errno value is returned
 *
 ****************************************************************************/
#define RPTUN_CONFIG(d, p) ((d)->ops->config ? \
                            (d)->ops->config(d, p) : 0)

/****************************************************************************
 * Name: RPTUN_START
 *
 * Description:
 *   START remote cpu
 *
 * Input Parameters:
 *   dev  - Device-specific state data
 *
 * Returned Value:
 *   OK unless an error occurs.  Then a negated errno value is returned
 *
 ****************************************************************************/

#define RPTUN_START(d) ((d)->ops->start ? \
                        (d)->ops->start(d) : -ENOSYS)

/****************************************************************************
 * Name: RPTUN_STOP
 *
 * Description:
 *   STOP remote cpu
 *
 * Input Parameters:
 *   dev  - Device-specific state data
 *
 * Returned Value:
 *   OK unless an error occurs.  Then a negated errno value is returned
 *
 ****************************************************************************/

#define RPTUN_STOP(d) ((d)->ops->stop ? \
                       (d)->ops->stop(d) : -ENOSYS)

/****************************************************************************
 * Name: RPTUN_NOTIFY
 *
 * Description:
 *   Notify remote core there is a message to get.
 *
 * Input Parameters:
 *   dev  - Device-specific state data
 *   vqid - Message to notify
 *
 * Returned Value:
 *   OK unless an error occurs.  Then a negated errno value is returned
 *
 ****************************************************************************/

#define RPTUN_NOTIFY(d,v) ((d)->ops->notify ? \
                           (d)->ops->notify(d,v) : -ENOSYS)

/****************************************************************************
 * Name: RPTUN_REGISTER_CALLBACK
 *
 * Description:
 *   Attach to receive a callback when something is received on RPTUN
 *
 * Input Parameters:
 *   dev      - Device-specific state data
 *   callback - The function to be called when something has been received
 *   arg      - A caller provided value to return with the callback
 *
 * Returned Value:
 *   OK unless an error occurs.  Then a negated errno value is returned
 *
 ****************************************************************************/

#define RPTUN_REGISTER_CALLBACK(d,c,a) ((d)->ops->register_callback ? \
                                        (d)->ops->register_callback(d,c,a) : -ENOSYS)

/****************************************************************************
 * Name: RPTUN_UNREGISTER_CALLBACK
 *
 * Description:
 *   Detach RPTUN callback
 *
 * Input Parameters:
 *   dev      - Device-specific state data
 *
 * Returned Value:
 *   OK unless an error occurs.  Then a negated errno value is returned
 *
 ****************************************************************************/

#define RPTUN_UNREGISTER_CALLBACK(d) ((d)->ops->register_callback ? \
                                      (d)->ops->register_callback(d,NULL,NULL) : -ENOSYS)

/****************************************************************************
 * Name: RPTUN_RESET
 *
 * Description:
 *   Reset remote cpu
 *
 * Input Parameters:
 *   dev      - Device-specific state data
 *   value    - reset value
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

#define RPTUN_RESET(d,v) ((d)->ops->reset ? \
                          (d)->ops->reset(d,v) : UNUSED(d))

/****************************************************************************
 * Name: RPTUN_PANIC
 *
 * Description:
 *   Panic remote cpu
 *
 * Input Parameters:
 *   dev      - Device-specific state data
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

#define RPTUN_PANIC(d) ((d)->ops->panic ? \
                        (d)->ops->panic(d) : UNUSED(d))

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef CODE int (*rptun_callback_t)(FAR void *arg, uint32_t vqid);

struct rptun_addrenv_s
{
  uintptr_t pa;
  uintptr_t da;
  size_t    size;
};

begin_packed_struct struct rptun_cmd_s
{
  uint32_t cmd_master;
  uint32_t cmd_slave;
} end_packed_struct;

struct aligned_data(8) rptun_rsc_s
{
  struct resource_table    rsc_tbl_hdr;
  uint32_t                 offset[2];
  struct fw_rsc_trace      log_trace;
  struct fw_rsc_vdev       rpmsg_vdev;
  struct fw_rsc_vdev_vring rpmsg_vring0;
  struct fw_rsc_vdev_vring rpmsg_vring1;
  struct fw_rsc_config     config;
};

struct rptun_dev_s;
struct rptun_ops_s
{
  CODE FAR const char *(*get_local_cpuname)(FAR struct rptun_dev_s *dev);
  CODE FAR const char *(*get_cpuname)(FAR struct rptun_dev_s *dev);
  CODE FAR const char *(*get_firmware)(FAR struct rptun_dev_s *dev);

  CODE FAR const struct rptun_addrenv_s *(*get_addrenv)(
                        FAR struct rptun_dev_s *dev);
  CODE FAR struct rptun_rsc_s *(*get_resource)(FAR struct rptun_dev_s *dev);

  CODE bool (*is_autostart)(FAR struct rptun_dev_s *dev);
  CODE bool (*is_master)(FAR struct rptun_dev_s *dev);

  CODE int (*config)(struct rptun_dev_s *dev, void *data);
  CODE int (*start)(FAR struct rptun_dev_s *dev);
  CODE int (*stop)(FAR struct rptun_dev_s *dev);
  CODE int (*notify)(FAR struct rptun_dev_s *dev, uint32_t vqid);
  CODE int (*register_callback)(FAR struct rptun_dev_s *dev,
                                rptun_callback_t callback, FAR void *arg);

  CODE void (*reset)(FAR struct rptun_dev_s *dev, int value);
  CODE void (*panic)(FAR struct rptun_dev_s *dev);
};

struct rptun_dev_s
{
  FAR const struct rptun_ops_s *ops;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

int rptun_initialize(FAR struct rptun_dev_s *dev);
int rptun_boot(FAR const char *cpuname);
int rptun_poweroff(FAR const char *cpuname);
int rptun_reset(FAR const char *cpuname, int value);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_RPTUN */
#endif /* __INCLUDE_NUTTX_RPTUN_RPTUN_H */
