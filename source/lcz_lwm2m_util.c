/**
 * @file lcz_lwm2m_util.c
 * @brief
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lcz_lwm2m_util, CONFIG_LCZ_LWM2M_UTIL_LOG_LEVEL);

/**************************************************************************************************/
/* Includes                                                                                       */
/**************************************************************************************************/
#include <zephyr/zephyr.h>
#include <zephyr/init.h>
#include <zephyr/net/lwm2m.h>

#if defined(CONFIG_LCZ_LWM2M_UTIL_CONFIG_DATA)
#include <file_system_utilities.h>
#endif

#if defined(CONFIG_LCZ_LWM2M_UTIL_MANAGE_OBJ_INST)
#include <lcz_lwm2m_gateway_obj.h>
#endif

#if defined(CONFIG_LCZ_LWM2M_UTIL_FWK_BROADCAST_ON_CREATE)
#include <fwk_includes.h>
#endif

#include <lwm2m_resource_ids.h>
#include <lcz_snprintk.h>
#include "lcz_lwm2m_util.h"

/**************************************************************************************************/
/* Local Constant, Macro and Type Definitions                                                     */
/**************************************************************************************************/
#define CFG_PATH CONFIG_FSU_MOUNT_POINT "/lwm2m_cfg/"

/* <path>/65535.65535.65535.65535 */
#define CFG_FILE_NAME_MAX_SIZE (sizeof(CFG_PATH) + LWM2M_MAX_PATH_STR_LEN + 1)

#define MANAGE_OBJS CONFIG_LCZ_LWM2M_UTIL_MANAGE_OBJ_INST

#if MANAGE_OBJS
/* The total number of object instances [sensors] per gateway object instance */
#define MAX_NODES CONFIG_LCZ_LWM2M_UTIL_MAX_NODES

#define MAX_INSTANCES CONFIG_LWM2M_GATEWAY_MAX_INSTANCES

enum lwm2m_create_state { CREATE_ALLOW = 0, CREATE_OK = 1, CREATE_FAIL = 2 };

/* Keep track of the creation state of each node */
struct node {
	enum lwm2m_create_state create_state;
	uint16_t type;
	uint16_t instance;
};

/* For each base/gateway object instance, there can be multiple [sensor] nodes */
struct node_list {
	uint16_t base_instance;
	struct node node[MAX_NODES];
};
#endif

/* The total number managed base instances is the number of gateway objects.
 * Other instances must be managed by application.
 */
struct lcz_lwm2m_util {
	struct k_mutex mutex;
	sys_slist_t obj_agents;
#if MANAGE_OBJS
	struct node_list node_list[MAX_INSTANCES];
#endif
};

/**************************************************************************************************/
/* Local Data Definitions                                                                         */
/**************************************************************************************************/
static struct lcz_lwm2m_util utl;

/**************************************************************************************************/
/* Local Function Prototypes                                                                      */
/**************************************************************************************************/
static int create_obj_inst(int idx, uint16_t type, uint16_t instance);
static int creation_callback(int idx, uint16_t type, uint16_t instance);

#if MANAGE_OBJS
static int gw_obj_deleted_handler(int idx);
static inline void reset_node(struct node *node);
static void gateway_obj_deleted_callback(int idx, void *data_ptr);
static struct node *find_node(struct node_list *node_list, uint16_t type, uint16_t offset);
static struct node *find_unused_node(struct node_list *node_list);
#endif

/**************************************************************************************************/
/* SYS INIT                                                                                       */
/**************************************************************************************************/
static int lcz_lwm2m_util_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	k_mutex_init(&utl.mutex);
	sys_slist_init(&utl.obj_agents);

#if MANAGE_OBJS
	lcz_lwm2m_gw_obj_set_telem_delete_cb(gateway_obj_deleted_callback);
#endif

#if defined(CONFIG_LCZ_LWM2M_UTIL_CONFIG_DATA)
	fsu_mkdir_abs(CFG_PATH, true);
#endif

	return 0;
}

SYS_INIT(lcz_lwm2m_util_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/**************************************************************************************************/
/* Global Function Definitions                                                                    */
/**************************************************************************************************/
void lcz_lwm2m_util_register_agent(struct lwm2m_obj_agent *agent)
{
	k_mutex_lock(&utl.mutex, K_FOREVER);
	sys_slist_append(&utl.obj_agents, &agent->node);
	k_mutex_unlock(&utl.mutex);
}

#if MANAGE_OBJS
int lcz_lwm2m_util_manage_obj_instance(uint16_t type, int idx, uint16_t offset)
{
	int r;
	int base_instance;
	int instance;
	struct node_list *node_list = NULL;
	struct node *node = NULL;

	k_mutex_lock(&utl.mutex, K_FOREVER);
	do {
		r = lcz_lwm2m_gw_obj_get_instance(idx);
		if (r < 0) {
			break;
		}

		/* The index [Bluetooth address] is valid and there is a valid
		 * gateway object instance for the device.  Now determine if the
		 * object instance for a particular [sensor] object needs to be created.
		 */
		base_instance = r;
		instance = r + offset;

		/* If the object list hasn't been set for base instance, then set it. */
		node_list = lcz_lwm2m_gw_obj_get_telem_data(idx);
		if (node_list == NULL) {
			node_list = &utl.node_list[idx];
			node_list->base_instance = base_instance;
			r = lcz_lwm2m_gw_obj_set_telem_data(idx, node_list);
			if (r < 0) {
				LOG_ERR("Unable to set telemetry data");
				break;
			}
		} else if (node_list->base_instance != base_instance) {
			LOG_ERR("Base Instance Mismatch");
			r = -EPERM;
			break;
		}

		node = find_node(node_list, type, instance);
		if (node) {
			/* Has it already been created? */
			if (node->create_state == CREATE_OK) {
				r = instance;
				break;
			} else if (node->create_state == CREATE_FAIL) {
				/* Creation can fail for other reasons, but not enough instances is most likely */
				r = -ENOMEM;
				break;
			} else {
				LOG_WRN("unexpected create state");
			}
		} else {
			node = find_unused_node(node_list);
			if (!node) {
				r = -ENOMEM;
				break;
			}
		}

		/* Try to create object instance */
		node->type = type;
		node->instance = instance;
		r = create_obj_inst(idx, type, instance);
		if (r == 0) {
			node->create_state = CREATE_OK;
			r = instance;
		} else {
			node->create_state = CREATE_FAIL;
		}

	} while (0);
	k_mutex_unlock(&utl.mutex);

	LOG_DBG("%d", r);
	return r;
}

int lcz_lwm2m_util_manage_obj_deletion(int status, uint16_t type, int idx, uint16_t instance)
{
	int r = 0;
	struct node_list *node_list = NULL;
	struct node *node = NULL;

	if (status != -EEXIST && status != -ENOENT) {
		return 0;
	}

	k_mutex_lock(&utl.mutex, K_FOREVER);
	do {
		node_list = lcz_lwm2m_gw_obj_get_telem_data(idx);
		if (node_list == NULL) {
			r = -ENOENT;
			break;
		}

		node = find_node(node_list, type, instance);
		if (node) {
			reset_node(node);
			r = 0;
		} else {
			LOG_ERR("Unable to find matching node");
			r = -ENOENT;
			break;
		}
	} while (0);
	k_mutex_unlock(&utl.mutex);

	return r;
}
#endif /* MANAGE_OBJS */

#if defined(CONFIG_LCZ_LWM2M_UTIL_CONFIG_DATA)
int lcz_lwm2m_util_load_config(uint16_t type, uint16_t instance, uint16_t resource,
			       uint16_t data_len)
{
	int r = -EPERM;
	char path[LWM2M_MAX_PATH_STR_LEN];
	char data[CONFIG_LCZ_LWM2M_UTIL_CONFIG_DATA_MAX_SIZE];
	char fname[CFG_FILE_NAME_MAX_SIZE];

	if (data_len == 0) {
		return -EINVAL;
	}

	if (data_len > sizeof(data)) {
		LOG_ERR("Unsupported size");
		return -ENOMEM;
	}

	do {
		/* Path is used as filename.  Instance IDs must be static for this to work properly. */
		LCZ_SNPRINTK(path, "%u/%u/%u", type, instance, resource);
		LCZ_SNPRINTK(fname, CFG_PATH "%u.%u.%u", type, instance, resource);
		r = (int)fsu_read_abs(fname, data, data_len);
		if (r < 0) {
			LOG_WRN("Unable to load %s: %d", fname, r);
			return r;
		}

		r = lwm2m_engine_set_opaque(path, data, data_len);
		if (r < 0) {
			LOG_ERR("Unable to set %s: %d", path, r);
			break;
		}

	} while (0);

	return r;
}

int lcz_lwm2m_util_save_config(uint16_t type, uint16_t instance, uint16_t resource, uint8_t *data,
			       uint16_t data_len)
{
	char fname[CFG_FILE_NAME_MAX_SIZE];
	int r = -EPERM;

	if (data == NULL) {
		r = -EIO;
	} else if (data_len == 0 || data_len > CONFIG_LCZ_LWM2M_UTIL_CONFIG_DATA_MAX_SIZE) {
		r = -EINVAL;
	} else if (fsu_lfs_mount() == 0) {
		LCZ_SNPRINTK(fname, CFG_PATH "%u.%u.%u", type, instance, resource);

		r = (int)fsu_write_abs(fname, data, data_len);
	}

	LOG_INF("Config save for %s status: %d", fname, r);

	return r;
}
#endif /* CONFIG_LCZ_LWM2M_UTIL_CONFIG_DATA */

int lcz_lwm2m_util_del_res_inst(uint16_t type, uint16_t instance, uint16_t resource,
				uint16_t resource_inst)
{
	char path[CFG_FILE_NAME_MAX_SIZE];

	LCZ_SNPRINTK(path, "%u/%u/%u/%u", type, instance, resource, resource_inst);

	return lwm2m_engine_delete_res_inst(path);
}

int lcz_lwm2m_util_reg_post_write_cb(uint16_t type, uint16_t instance, uint16_t resource,
				     lwm2m_engine_set_data_cb_t cb)
{
	char path[LWM2M_MAX_PATH_STR_LEN];

	LCZ_SNPRINTK(path, "%u/%u/%u", type, instance, resource);

	return lwm2m_engine_register_post_write_callback(path, cb);
}

int lcz_lwm2m_util_create_obj_inst(uint16_t type, uint16_t instance)
{
#ifdef CONFIG_LCZ_LWM2M_UTIL_MANAGE_OBJ_INST
	if (instance < CONFIG_LCZ_LWM2M_GATEWAY_OBJ_LEGACY_INST_OFFSET) {
		return -EINVAL;
	} else
#endif
	{
		/* Index not used when unmanaged */
		return create_obj_inst(-1, type, instance);
	}
}

int lcz_lwm2m_util_delete_obj_instance(uint16_t type, uint16_t instance)
{
	char path[LWM2M_MAX_PATH_STR_LEN];

	LCZ_SNPRINTK(path, "%u/%u", type, instance);

	return lwm2m_engine_delete_obj_inst(path);
}

/**************************************************************************************************/
/* Local Function Definitions                                                                     */
/**************************************************************************************************/
static int create_obj_inst(int idx, uint16_t type, uint16_t instance)
{
	char path[LWM2M_MAX_PATH_STR_LEN];
	int r;

	do {
		LCZ_SNPRINTK(path, "%u/%u", type, instance);
		r = lwm2m_engine_create_obj_inst(path);
		if (r < 0) {
			break;
		}

		r = creation_callback(idx, type, instance);
		if (r < 0) {
			break;
		}

#if defined(CONFIG_LCZ_LWM2M_UTIL_FWK_BROADCAST_ON_CREATE)
		FRAMEWORK_MSG_CREATE_AND_BROADCAST(FWK_ID_RESERVED, FMC_LWM2M_OBJ_CREATED);
#endif

	} while (0);

	return r;
}

static int creation_callback(int idx, uint16_t type, uint16_t instance)
{
	sys_snode_t *node;
	struct lwm2m_obj_agent *agent;

	/* Find the custom creation function for the object type */
	SYS_SLIST_FOR_EACH_NODE (&utl.obj_agents, node) {
		agent = CONTAINER_OF(node, struct lwm2m_obj_agent, node);
		if (agent->type == type) {
			if (agent->create != NULL) {
				return agent->create(idx, type, instance, agent->context);
			}
		}
	}

	return 0;
}

#if MANAGE_OBJS
static struct node *find_node(struct node_list *node_list, uint16_t type, uint16_t instance)
{
	struct node *node = NULL;
	int i;

	for (i = 0; i < MAX_NODES; i++) {
		if ((node_list->node[i].type == type) &&
		    (node_list->node[i].instance == instance)) {
			node = &node_list->node[i];
			break;
		}
	}

	return node;
}

static struct node *find_unused_node(struct node_list *node_list)
{
	struct node *node = NULL;
	int i;

	for (i = 0; i < MAX_NODES; i++) {
		if ((node_list->node[i].create_state == CREATE_ALLOW)) {
			node = &node_list->node[i];
			break;
		}
	}

	if (!node) {
		LOG_ERR("Not enough lwm2m object nodes");
	}

	return node;
}

/* reset node assumes mutex locked */
static void reset_node(struct node *node)
{
	if (node) {
		LOG_DBG("Reset node type: %u instance %u", node->type, node->instance);
		node->create_state = CREATE_ALLOW;
		node->type = 0;
		node->instance = 0;
	} else {
		LOG_ERR("Invalid node");
	}
}

/* If an object has been removed, then a previously failed create may now succeed. */
static void allow_create_on_delete(uint16_t type)
{
	int i;
	int j;
	int count = 0;
	struct node *node;

	for (j = 0; j < MAX_INSTANCES; j++) {
		for (i = 0; i < MAX_NODES; i++) {
			node = &utl.node_list[j].node[i];
			if (node->type == type && node->create_state == CREATE_FAIL) {
				reset_node(node);
				count += 1;
			}
		}
	}

	LOG_DBG("reset %d nodes in the create fail state", count);
}

static void gateway_obj_deleted_callback(int idx, void *data_ptr)
{
	int base_instance;
	int instance;
	int i;
	struct node_list *node_list = data_ptr;

	base_instance = lcz_lwm2m_gw_obj_get_instance(idx);
	if (base_instance < 0) {
		LOG_ERR("Invalid instance");
		return;
	}

	if (node_list == NULL) {
		/* An object may not have been created for this instance */
		LOG_DBG("Invalid node list");
		return;
	}

	if (node_list->base_instance != base_instance) {
		LOG_ERR("Base Instance Mismatch");
		return;
	}

	/* Delete any [sensor] objects for this device. */
	k_mutex_lock(&utl.mutex, K_FOREVER);
	for (i = 0; i < MAX_NODES; i++) {
		if (node_list->node[i].create_state == CREATE_OK) {
			instance = node_list->node[i].instance;
			lcz_lwm2m_util_delete_obj_instance(node_list->node[i].type, instance);
			allow_create_on_delete(node_list->node[i].type);
		}
		reset_node(&node_list->node[i]);
	}
	k_mutex_unlock(&utl.mutex);

	gw_obj_deleted_handler(idx);
}

static int gw_obj_deleted_handler(int idx)
{
	sys_snode_t *node;
	struct lwm2m_obj_agent *agent;

	SYS_SLIST_FOR_EACH_NODE (&utl.obj_agents, node) {
		agent = CONTAINER_OF(node, struct lwm2m_obj_agent, node);
		if (agent->gw_obj_deleted != NULL) {
			return agent->gw_obj_deleted(idx, agent->context);
		}
	}

	return 0;
}
#endif