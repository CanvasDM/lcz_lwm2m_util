/**
 * @file lcz_lwm2m_util.h
 * @brief Utility library for creating and deleting object instances.
 * If the gateway object is supported, then instances are can also be managed.
 *
 * Copyright (c) 2022 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __LCZ_LWM2M_UTIL_H__
#define __LCZ_LWM2M_UTIL_H__

/**************************************************************************************************/
/* Includes                                                                                       */
/**************************************************************************************************/
#include <zephyr/zephyr.h>
#include <zephyr/sys/slist.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/net/lwm2m.h>

#ifdef __cplusplus
extern "C" {
#endif

/**************************************************************************************************/
/* Global Constants, Macros and Type Definitions                                                  */
/**************************************************************************************************/
struct lwm2m_obj_agent {
	sys_snode_t node;
	/* Object instanced type */
	uint16_t type;
	/* User data provided in callback */
	void *context;
	/* Callback that occurs after object instance is successfully created.
	 * Index is -1 when callback managed objects aren't used.
	 */
	int (*create)(int idx, uint16_t type, uint16_t instance, void *context);
	/* Callback that occurs when gateway object is deleted */
#if defined(CONFIG_LCZ_LWM2M_UTIL_MANAGE_OBJ_INST)
	int (*gw_obj_deleted)(int idx, void *context);
#endif
};

#define LCZ_LWM2M_UTIL_USER_INIT_PRIORITY 95
BUILD_ASSERT(LCZ_LWM2M_UTIL_USER_INIT_PRIORITY > CONFIG_APPLICATION_INIT_PRIORITY,
	     "LwM2M utilities must initialize before users");

/**************************************************************************************************/
/* Global Function Prototypes                                                                     */
/**************************************************************************************************/

/**
 * @brief Register creation and deletion callbacks
 *
 * @param agent is a linked list node of callbacks
 */
void lcz_lwm2m_util_register_agent(struct lwm2m_obj_agent *agent);

/**
 * @brief Get instance id for object from gateway.
 * Application may need to call @ref lcz_lwm2m_gw_obj_create before this.
 * If object doesn't exist, then try to create it.
 * Object instances are deleted if gateway object is deleted.
 *
 * @param type LwM2M object instance type
 * @param idx index into gateway object table
 * @param offset of instance, when multiple instances of same sensor is present
 * For example, a BT610 may have 4 temperature sensors with offsets of 0, 1, 2, and 3.
 * @return int negative error code, otherwise instance number
 */
int lcz_lwm2m_util_manage_obj_instance(uint16_t type, int idx, uint16_t offset);

/**
 * @brief Inform manager that the object doesn't exist.
 * @note Putting this burden on the [sensor] instance is the simplest method to
 * handle deletion of objects by the server.
 *
 * @param status of LwM2M engine call (e.g., set)
 * @param type LwM2M object instance type
 * @param idx index into gateway object table
 * @param instance unique ID
 * @return int negative error code, otherwise 0
 */
int lcz_lwm2m_util_manage_obj_deletion(int status, uint16_t type, int idx, uint16_t instance);

/**
 * @brief Create LwM2M object instance. Wraps engine call with path generation.
 * If object instance is created, then registered create callbacks will be issued.
 *
 * @param type of object
 * @param instance unique ID managed by caller
 * @return int negative error code, 0 on success
 */
int lcz_lwm2m_util_create_obj_inst(uint16_t type, uint16_t instance);

/**
 * @brief Delete LwM2M object instance. Wraps engine call with path generation.
 *
 * @param type of object
 * @param instance unique ID
 * @return int negative error code, 0 on success
 */
int lcz_lwm2m_util_delete_obj_instance(uint16_t type, uint16_t instance);

/**
 * @brief Load configuration data for a resource.  The file name is the same
 * as the path (type.instance.resource). For example, "3435.62812.1" is for a filling
 * sensor with instance 62812 and resource container height.
 *
 * @note For this to work properly; instance IDs must be static.
 *
 * @param type of object
 * @param instance ID
 * @param resource ID
 * @param data_len length of data
 * @return int negative error code, otherwise number of bytes read from file
 */
int lcz_lwm2m_util_load_config(uint16_t type, uint16_t instance, uint16_t resource,
			       uint16_t data_len);

/**
 * @brief Save configuration data for a resource that can be loaded at another time (after
 * reboot).  This allows LwM2M Cloud Interface to save configuration data for a sensor.
 *
 * @param type of object
 * @param instance ID
 * @param resource ID
 * @param data_len length of data
 * @return int negative error code, otherwise number of bytes read from file
 */
int lcz_lwm2m_util_save_config(uint16_t type, uint16_t instance, uint16_t resource, uint8_t *data,
			       uint16_t data_len);

/**
 * @brief Delete resource instance.  Wraps engine call with path generation.
 *
 * @param type ID of object
 * @param instance ID
 * @param resource ID
 * @param resource_inst ID
 * @return int negative error code, otherwise 0 on success
 */
int lcz_lwm2m_util_del_res_inst(uint16_t type, uint16_t instance, uint16_t resource,
				uint16_t resource_inst);

/**
 * @brief Register for a callback after a write occurs to a specific resource instance.
 * Wraps engine call with path generation.
 * This can be used to save configuration data when it is written from the server.
 *
 * @param type ID of object
 * @param instance ID
 * @param resource ID
 * @param cb callback
 * @return int negative error code, 0 on success
 */
int lcz_lwm2m_util_reg_post_write_cb(uint16_t type, uint16_t instance, uint16_t resource,
				     lwm2m_engine_set_data_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* __LCZ_LWM2M_UTIL_H__ */
