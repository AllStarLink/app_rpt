
struct ast_bridge *rpt_pseudo_bridge(const char *name);

void rpt_pseudo_bridge_unref(struct ast_bridge *bridge);

int pseudo_channel_push(struct ast_channel *ast, struct ast_bridge *bridge, struct ast_bridge_features *features);

void rpt_unregister_pseudo_channel_tech(void);

int rpt_register_pseudo_channel_tech(void);
