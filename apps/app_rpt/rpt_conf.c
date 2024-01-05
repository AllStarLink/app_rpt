/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2023 Naveen Albert
 *
 * Naveen Albert <asterisk@phreaknet.org>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*!
 * \file
 *
 * \brief Repeater pseudo bridge channel driver
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 *
 */

#include "asterisk.h"

#include "asterisk/channel.h"
#include "asterisk/bridge.h"
#include "asterisk/core_unreal.h"

#include "rpt_conf.h"

/*
 * This embedded channel driver is meant to be a drop in for the idiom of:
 * - requesting a pseudo channel
 * - adding it to a DAHDI conference using DAHDI_SETCONF
 * - handling primitive conferencing functionality as DAHDI does
 *
 * e.g. instead of ast_request("DAHDI", cap, NULL, NULL, "pseudo", NULL),
 * you can use ast_request("Pseudo", cap, NULL, NULL, "foobar-123", NULL)
 */

static struct ast_channel_tech *pseudo_get_tech(void);

/*! Pseudo bridge container */
struct pseudo_bridge {
	struct ast_bridge *bridge;
	const char *name;
	unsigned int usecount;
	AST_RWLIST_ENTRY(pseudo_bridge) entry;
	char data[];
};

static AST_RWLIST_HEAD_STATIC(pseudo_bridges, pseudo_bridge);

/*! Channel private. */
struct pseudo_pvt {
	/*! Unreal channel driver base class values. */
	struct ast_unreal_pvt base;
	/*! Conference bridge associated with this pseudo. */
	struct ast_bridge *bridge;
};

/*! \brief Create a new bridge */
static struct ast_bridge *pseudo_bridge_new(const char *name)
{
	struct ast_bridge *bridge;

	bridge = ast_bridge_base_new(AST_BRIDGE_CAPABILITY_MULTIMIX,
		AST_BRIDGE_FLAG_MASQUERADE_ONLY | AST_BRIDGE_FLAG_TRANSFER_BRIDGE_ONLY, name, NULL, NULL);
	if (!bridge) {
		ast_log(LOG_ERROR, "Failed to allocate bridge\n");
		return NULL;
	}
	ast_bridge_set_internal_sample_rate(bridge, 8000); /* 8 KHz */
	ast_bridge_set_maximum_sample_rate(bridge, 8000);
	ast_bridge_set_mixing_interval(bridge, 20); /* 20 ms */
	return bridge;
}

static int rpt_pseudo_bridge_ref(struct ast_bridge *bridge)
{
	struct pseudo_bridge *pb;
	AST_RWLIST_WRLOCK(&pseudo_bridges);
	AST_RWLIST_TRAVERSE(&pseudo_bridges, pb, entry) {
		if (pb->bridge == bridge) {
			pb->usecount++;
			ast_debug(1, "Pseudo bridge %s now has use count %u\n", pb->name, pb->usecount);
			break;
		}
	}
	AST_RWLIST_UNLOCK(&pseudo_bridges);
	if (!pb) {
		ast_log(LOG_ERROR, "Bridge %p not in container\n", bridge);
	}
	return pb ? 0 : -1;
}

/*! \brief Create or reuse an existing bridge */
struct ast_bridge *rpt_pseudo_bridge(const char *name)
{
	struct pseudo_bridge *pb;

	AST_RWLIST_WRLOCK(&pseudo_bridges);
	AST_RWLIST_TRAVERSE(&pseudo_bridges, pb, entry) {
		if (!strcasecmp(pb->name, name)) {
			break;
		}
	}
	if (!pb) {
		pb = ast_calloc(1, sizeof(*pb) + strlen(name) + 1);
		if (!pb) {
			AST_RWLIST_UNLOCK(&pseudo_bridges);
			return NULL;
		}
		strcpy(pb->data, name); /* Safe */
		pb->name = pb->data;
		pb->bridge = pseudo_bridge_new(name);
		if (!pb->bridge) {
			ast_free(pb);
			AST_RWLIST_UNLOCK(&pseudo_bridges);
			return NULL;
		}
		AST_RWLIST_INSERT_HEAD(&pseudo_bridges, pb, entry);
	}
	pb->usecount++;
	ast_debug(1, "Pseudo bridge %s now has use count %d\n", name, pb->usecount);
	AST_RWLIST_UNLOCK(&pseudo_bridges);
	return pb->bridge;
}

void rpt_pseudo_bridge_unref(struct ast_bridge *bridge)
{
	struct pseudo_bridge *pb;

	AST_RWLIST_WRLOCK(&pseudo_bridges);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&pseudo_bridges, pb, entry) {
		if (bridge == pb->bridge) {
			pb->usecount--;
			ast_debug(1, "Pseudo bridge %s now has use count %u\n", pb->name, pb->usecount);
			if (!pb->usecount) {
				ast_debug(1, "Destroying pseudo bridge %s (%p)\n", pb->name, pb);
				//AST_RWLIST_REMOVE_CURRENT(entry);
				do {
					__list_current->entry.next = NULL;
					__list_current = __list_prev;
					if (__list_prev) {
						__list_prev->entry.next = __list_next;
					} else {
						__list_head->first = __list_next;
					}
					if (!__list_next) {
						__list_head->last = __list_prev;
					}
				} while (0);
				/* Clean up the bridge, since it's no longer being used. */
				ast_bridge_destroy(pb->bridge, 0); /* Don't care about cause code */
				ast_free(pb);
			}
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&pseudo_bridges);
	if (!pb) {
		ast_log(LOG_ERROR, "Couldn't find pseudo bridge %p?\n", bridge);
	}
}

static void pseudo_pvt_destructor(void *vdoomed)
{
	struct pseudo_pvt *pvt = vdoomed;

	if (pvt->bridge) {
		/* We are no longer using this bridge. */
		rpt_pseudo_bridge_unref(pvt->bridge);
		pvt->bridge = NULL;
	}
	ast_unreal_destructor(&pvt->base);
}

static int pseudo_call(struct ast_channel *chan, const char *addr, int timeout)
{
	/* Make sure anyone calling ast_call() for this channel driver is going to fail. */
	return -1;
}

static int pseudo_hangup(struct ast_channel *ast)
{
	struct pseudo_pvt *p = ast_channel_tech_pvt(ast);
	int res;

	if (!p) {
		return -1;
	}

	/* give the pvt a ref to fulfill calling requirements. */
	ao2_ref(p, +1);
	res = ast_unreal_hangup(&p->base, ast);
	ao2_ref(p, -1);

	return res;
}

static struct ast_channel *pseudo_request(const char *type, struct ast_format_cap *cap,
	const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause)
{
	struct ast_channel *chan;
	const char *conf_name = data;
	RAII_VAR(struct pseudo_pvt *, pvt, NULL, ao2_cleanup);

	/* Channels in a bridge cannot be serviced directly (without suspending the bridge for that channel),
	 * so if we want to be able to perform poll, read, and write operations
	 * on this channel as normal,
	 * then chan itself cannot actually be added to the bridge.
	 * Instead, we create a local channel pair.
	 * One side of this pair is connected to read/write */

	/* Allocate a new private structure and then Asterisk channels */
	pvt = (struct pseudo_pvt *) ast_unreal_alloc(sizeof(*pvt), pseudo_pvt_destructor, cap);
	if (!pvt) {
		return NULL;
	}
	ast_set_flag(&pvt->base, AST_UNREAL_NO_OPTIMIZATION);
	ast_copy_string(pvt->base.name, conf_name, sizeof(pvt->base.name));

	chan = ast_unreal_new_channels(&pvt->base, pseudo_get_tech(),
		AST_STATE_UP, AST_STATE_UP, NULL, NULL, assignedids, requestor, 0);
	if (chan) {
		ast_answer(pvt->base.owner);
		ast_answer(pvt->base.chan);
#if 0
		if (ast_channel_add_bridge_role(pvt->base.chan, "announcer")) {
			ast_hangup(chan);
			chan = NULL;
		}
#endif
	}

	return chan;
}

int pseudo_channel_push(struct ast_channel *ast, struct ast_bridge *bridge, struct ast_bridge_features *features)
{
	struct ast_channel *chan;
	RAII_VAR(struct pseudo_pvt *, p, NULL, ao2_cleanup);

	if (!bridge) {
		ast_log(LOG_ERROR, "No bridge provided\n");
		return -1;
	}
	if (!features) {
		ast_log(LOG_ERROR, "No features provided\n");
		return -1;
	}
	if (strcasecmp(ast_channel_tech(ast)->type, "RPTpseudo")) {
		ast_log(LOG_ERROR, "%s is not an RPTpseudo channel\n", ast_channel_name(ast));
		return -1;
	}

	{
		SCOPED_CHANNELLOCK(lock, ast);

		p = ast_channel_tech_pvt(ast);
		if (!p) {
			ast_log(LOG_ERROR, "%s is not an RPT pseudo channel\n", ast_channel_name(ast));
			return -1;
		}
		ao2_ref(p, +1);
		chan = p->base.chan;
		if (!chan) {
			ast_log(LOG_ERROR, "No base channel?\n");
			return -1;
		}
	}

	/* Link the bridge */
	p->bridge = bridge;
	ast_set_flag(&features->feature_flags, AST_BRIDGE_CHANNEL_FLAG_IMMOVABLE);

	/* Impart the output channel into the bridge */
	rpt_pseudo_bridge_ref(p->bridge);
	if (ast_bridge_impart(p->bridge, chan, NULL, features, AST_BRIDGE_IMPART_CHAN_INDEPENDENT)) {
		rpt_pseudo_bridge_unref(p->bridge);
		return -1;
	}
	ao2_lock(p);
	ast_set_flag(&p->base, AST_UNREAL_CARETAKER_THREAD);
	ao2_unlock(p);
	return 0;
}

static struct ast_channel_tech pseudo_tech = {
	.type = "RPTpseudo",
	.description = "Repeater Pseudo Bridge Channel",
	.requester = pseudo_request,
	.call = pseudo_call,
	.hangup = pseudo_hangup,

	.send_digit_begin = ast_unreal_digit_begin,
	.send_digit_end = ast_unreal_digit_end,
	.read = ast_unreal_read,
	.write = ast_unreal_write,
	.write_video = ast_unreal_write,
	.exception = ast_unreal_read,
	.indicate = ast_unreal_indicate,
	.fixup = ast_unreal_fixup,
	.send_html = ast_unreal_sendhtml,
	.send_text = ast_unreal_sendtext,
	.queryoption = ast_unreal_queryoption,
	.setoption = ast_unreal_setoption,
	.properties = AST_CHAN_TP_INTERNAL,
};

static struct ast_channel_tech *pseudo_get_tech(void)
{
	return &pseudo_tech;
}

void rpt_unregister_pseudo_channel_tech(void)
{
	struct ast_channel_tech *tech = &pseudo_tech;
	ast_channel_unregister(tech);
	ao2_cleanup(tech->capabilities);
}

int rpt_register_pseudo_channel_tech(void)
{
	struct ast_channel_tech *tech = &pseudo_tech;
	tech->capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!tech->capabilities) {
		return -1;
	}
	ast_format_cap_append_by_type(tech->capabilities, AST_MEDIA_TYPE_UNKNOWN);
	if (ast_channel_register(tech)) {
		ast_log(LOG_ERROR, "Unable to register channel technology %s(%s).\n", tech->type, tech->description);
		return -1;
	}
	return 0;
}
