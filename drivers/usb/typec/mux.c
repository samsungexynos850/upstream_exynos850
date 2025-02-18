// SPDX-License-Identifier: GPL-2.0
/**
 * USB Type-C Multiplexer/DeMultiplexer Switch support
 *
 * Copyright (C) 2018 Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 *         Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/device.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/usb/typec_mux.h>

#include "bus.h"

static DEFINE_MUTEX(switch_lock);
static DEFINE_MUTEX(mux_lock);
static LIST_HEAD(switch_list);
static LIST_HEAD(mux_list);

static void *typec_switch_match(struct device_connection *con, int ep,
				void *data)
{
	struct typec_switch *sw;

	if (!con->fwnode) {
		list_for_each_entry(sw, &switch_list, entry)
			if (!strcmp(con->endpoint[ep], dev_name(sw->dev)))
				return sw;
		return ERR_PTR(-EPROBE_DEFER);
	}

	/*
	 * With OF graph the mux node must have a boolean device property named
	 * "orientation-switch".
	 */
	if (con->id && !fwnode_property_present(con->fwnode, con->id))
		return NULL;

	list_for_each_entry(sw, &switch_list, entry)
		if (dev_fwnode(sw->dev) == con->fwnode)
			return sw;

	return con->id ? ERR_PTR(-EPROBE_DEFER) : NULL;
}

/**
 * typec_switch_get - Find USB Type-C orientation switch
 * @dev: The caller device
 *
 * Finds a switch linked with @dev. Returns a reference to the switch on
 * success, NULL if no matching connection was found, or
 * ERR_PTR(-EPROBE_DEFER) when a connection was found but the switch
 * has not been enumerated yet.
 */
struct typec_switch *typec_switch_get(struct device *dev)
{
	struct typec_switch *sw;

	sw = device_connection_find_match(dev, "orientation-switch", NULL,
					  typec_switch_match);
	if (!IS_ERR_OR_NULL(sw))
		WARN_ON(!try_module_get(sw->dev->parent->driver->owner));

	return sw;
}
EXPORT_SYMBOL_GPL(typec_switch_get);

/**
 * typec_put_switch - Release USB Type-C orientation switch
 * @sw: USB Type-C orientation switch
 *
 * Decrement reference count for @sw.
 */
void typec_switch_put(struct typec_switch *sw)
{
	if (!IS_ERR_OR_NULL(sw)) {
		module_put(sw->dev->parent->driver->owner);
		put_device(sw->dev);
	}
}
EXPORT_SYMBOL_GPL(typec_switch_put);

/**
 * typec_switch_register - Register USB Type-C orientation switch
 * @parent: Parent device
 * @desc: Orientation switch description
 *
 * This function registers a switch that can be used for routing the correct
 * data pairs depending on the cable plug orientation from the USB Type-C
 * connector to the USB controllers. USB Type-C plugs can be inserted
 * right-side-up or upside-down.
 */
struct typec_switch *
typec_switch_register(struct device *parent,
		      const struct typec_switch_desc *desc)
{
	struct typec_switch *sw;
	int ret;

	if (!desc || !desc->set)
		return ERR_PTR(-EINVAL);

	sw = kzalloc(sizeof(*sw), GFP_KERNEL);
	if (!sw)
		return ERR_PTR(-ENOMEM);

	sw->set = desc->set;

	device_initialize(sw->dev);
	sw->dev->parent = parent;
	sw->dev->fwnode = desc->fwnode;
	sw->dev->class = &typec_mux_class;
	sw->dev->driver_data = desc->drvdata;
	ret = dev_set_name(sw->dev, "%s-switch", dev_name(parent));
	if (ret) {
		put_device(sw->dev);
		return ERR_PTR(ret);
	}

	ret = device_add(sw->dev);
	if (ret) {
		dev_err(parent, "failed to register switch (%d)\n", ret);
		put_device(sw->dev);
		return ERR_PTR(ret);
	}

	return sw;
}
EXPORT_SYMBOL_GPL(typec_switch_register);

/**
 * typec_switch_unregister - Unregister USB Type-C orientation switch
 * @sw: USB Type-C orientation switch
 *
 * Unregister switch that was registered with typec_switch_register().
 */
void typec_switch_unregister(struct typec_switch *sw)
{
	if (!IS_ERR_OR_NULL(sw))
		device_unregister(sw->dev);
}
EXPORT_SYMBOL_GPL(typec_switch_unregister);

void typec_switch_set_drvdata(struct typec_switch *sw, void *data)
{
	dev_set_drvdata(sw->dev, data);
}
EXPORT_SYMBOL_GPL(typec_switch_set_drvdata);

void *typec_switch_get_drvdata(struct typec_switch *sw)
{
	return dev_get_drvdata(sw->dev);
}
EXPORT_SYMBOL_GPL(typec_switch_get_drvdata);

/* ------------------------------------------------------------------------- */

static void *typec_mux_match(struct device_connection *con, int ep, void *data)
{
	const struct typec_altmode_desc *desc = data;
	struct typec_mux *mux;
	int nval;
	bool match;
	u16 *val;
	int i;

	if (!con->fwnode) {
		list_for_each_entry(mux, &mux_list, entry)
			if (!strcmp(con->endpoint[ep], dev_name(mux->dev)))
				return mux;
		return ERR_PTR(-EPROBE_DEFER);
	}

	/*
	 * Check has the identifier already been "consumed". If it
	 * has, no need to do any extra connection identification.
	 */
	match = !con->id;
	if (match)
		goto find_mux;

	/* Accessory Mode muxes */
	if (!desc) {
		match = fwnode_property_present(con->fwnode, "accessory");
		if (match)
			goto find_mux;
		return NULL;
	}

	/* Alternate Mode muxes */
	nval = fwnode_property_read_u16_array(con->fwnode, "svid", NULL, 0);
	if (nval <= 0)
		return NULL;

	val = kcalloc(nval, sizeof(*val), GFP_KERNEL);
	if (!val)
		return ERR_PTR(-ENOMEM);

	nval = fwnode_property_read_u16_array(con->fwnode, "svid", val, nval);
	if (nval < 0) {
		kfree(val);
		return ERR_PTR(nval);
	}

	for (i = 0; i < nval; i++) {
		match = val[i] == desc->svid;
		if (match) {
			kfree(val);
			goto find_mux;
		}
	}
	kfree(val);
	return NULL;

find_mux:
	list_for_each_entry(mux, &mux_list, entry)
		if (dev_fwnode(mux->dev) == con->fwnode)
			return mux;

	return match ? ERR_PTR(-EPROBE_DEFER) : NULL;
}

/**
 * typec_mux_get - Find USB Type-C Multiplexer
 * @dev: The caller device
 * @desc: Alt Mode description
 *
 * Finds a mux linked to the caller. This function is primarily meant for the
 * Type-C drivers. Returns a reference to the mux on success, NULL if no
 * matching connection was found, or ERR_PTR(-EPROBE_DEFER) when a connection
 * was found but the mux has not been enumerated yet.
 */
struct typec_mux *typec_mux_get(struct device *dev,
				const struct typec_altmode_desc *desc)
{
	struct typec_mux *mux;

	mux = device_connection_find_match(dev, "mode-switch", (void *)desc,
					   typec_mux_match);
	if (!IS_ERR_OR_NULL(mux))
		WARN_ON(!try_module_get(mux->dev->parent->driver->owner));

	return mux;
}
EXPORT_SYMBOL_GPL(typec_mux_get);

/**
 * typec_mux_put - Release handle to a Multiplexer
 * @mux: USB Type-C Connector Multiplexer/DeMultiplexer
 *
 * Decrements reference count for @mux.
 */
void typec_mux_put(struct typec_mux *mux)
{
	if (!IS_ERR_OR_NULL(mux)) {
		module_put(mux->dev->parent->driver->owner);
		put_device(mux->dev);
	}
}
EXPORT_SYMBOL_GPL(typec_mux_put);

/**
 * typec_mux_register - Register Multiplexer routing USB Type-C pins
 * @parent: Parent device
 * @desc: Multiplexer description
 *
 * USB Type-C connectors can be used for alternate modes of operation besides
 * USB when Accessory/Alternate Modes are supported. With some of those modes,
 * the pins on the connector need to be reconfigured. This function registers
 * multiplexer switches routing the pins on the connector.
 */
struct typec_mux *
typec_mux_register(struct device *parent, const struct typec_mux_desc *desc)
{
	struct typec_mux *mux;
	int ret;

	if (!desc || !desc->set)
		return ERR_PTR(-EINVAL);

	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return ERR_PTR(-ENOMEM);

	mux->set = desc->set;

	device_initialize(mux->dev);
	mux->dev->parent = parent;
	mux->dev->fwnode = desc->fwnode;
	mux->dev->class = &typec_mux_class;
	mux->dev->driver_data = desc->drvdata;
	ret = dev_set_name(mux->dev, "%s-mux", dev_name(parent));
	if (ret) {
		put_device(mux->dev);
		return ERR_PTR(ret);
	}

	ret = device_add(mux->dev);
	if (ret) {
		dev_err(parent, "failed to register mux (%d)\n", ret);
		put_device(mux->dev);
		return ERR_PTR(ret);
	}

	return mux;
}
EXPORT_SYMBOL_GPL(typec_mux_register);

/**
 * typec_mux_unregister - Unregister Multiplexer Switch
 * @mux: USB Type-C Connector Multiplexer/DeMultiplexer
 *
 * Unregister mux that was registered with typec_mux_register().
 */
void typec_mux_unregister(struct typec_mux *mux)
{
	if (!IS_ERR_OR_NULL(mux))
		device_unregister(mux->dev);
}
EXPORT_SYMBOL_GPL(typec_mux_unregister);

void typec_mux_set_drvdata(struct typec_mux *mux, void *data)
{
	dev_set_drvdata(mux->dev, data);
}
EXPORT_SYMBOL_GPL(typec_mux_set_drvdata);

void *typec_mux_get_drvdata(struct typec_mux *mux)
{
	return dev_get_drvdata(mux->dev);
}
EXPORT_SYMBOL_GPL(typec_mux_get_drvdata);

struct class typec_mux_class = {
	.name = "typec_mux",
	.owner = THIS_MODULE,
};
