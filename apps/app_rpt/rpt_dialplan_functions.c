
/*! \file
 *
 * \brief RPT dialplan functions
 */

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/channel.h"
#include "asterisk/app.h"

#include "app_rpt.h"
#include "rpt_dialplan_functions.h"

/*** DOCUMENTATION
	<function name="RPT_NODE" language="en_US">
		<synopsis>
			Retrieves information about a repeater node
		</synopsis>
		<syntax>
			<parameter name="node" required="true">
				<para>Node number</para>
			</parameter>
			<parameter name="field" required="true">
				<para>Information to retrieve</para>
				<enumlist>
					<enum name="exists">
						<para>Whether a node with this node number exists on this system.</para>
					</enum>
					<enum name="keyed">
						<para>Whether a node is currently keyed.</para>
					</enum>
					<enum name="txkeyed">
						<para>Whether a node's transmitter is currently keyed.</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Retrieves information about a repeater node, by node number.</para>
		</description>
	</function>
 ***/

extern struct rpt rpt_vars[MAXRPTS];

static int rpt_node_read(struct ast_channel *chan, const char *function, char *data, char *buf, size_t len)
{
	char *parse;
	struct rpt *rpt = NULL;
	int i, nrpts;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(nodenum);
		AST_APP_ARG(field);
	);

	*buf = '\0';

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "%s: Arguments required\n", function);
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.nodenum)) {
		ast_log(LOG_ERROR, "%s: Node number required\n", function);
		return -1;
	} else if (ast_strlen_zero(args.field)) {
		ast_log(LOG_ERROR, "%s: Field required\n", function);
		return -1;
	}

	/* Find the node */
	nrpts = rpt_num_rpts();
	for (i = 0; i < nrpts; i++) {
		if (!strcasecmp(rpt_vars[i].name, args.nodenum)) {
			rpt = &rpt_vars[i];
			break;
		}
	}

	if (!strcasecmp(args.field, "exists")) {
		snprintf(buf, len, "%d", rpt ? 1 : 0);
	} else if (!strcasecmp(args.field, "keyed")) {
		snprintf(buf, len, "%d", rpt && rpt->keyed ? 1 : 0);
	} else if (!strcasecmp(args.field, "txkeyed")) {
		snprintf(buf, len, "%d", rpt && rpt->txkeyed ? 1 : 0);
	} else {
		ast_log(LOG_ERROR, "%s: Invalid field '%s'\n", function, args.field);
		return -1;
	}
	return 0;
}

static struct ast_custom_function rpt_node_function = {
	.name = "RPT_NODE",
	.read = rpt_node_read,
};

int rpt_dialplan_funcs_load(void)
{
	int res = 0;
	res |= ast_custom_function_register(&rpt_node_function);
	return res;
}

int rpt_dialplan_funcs_unload(void)
{
	int res = 0;
	res |= ast_custom_function_unregister(&rpt_node_function);
	return res;
}
