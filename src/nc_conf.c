/*
 * twemproxy - A fast and lightweight proxy for memcached protocol.
 * Copyright (C) 2011 Twitter, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <nc_core.h>
#include <nc_conf.h>
#include <nc_server.h>
#include <proto/nc_proto.h>

#define DEFINE_ACTION(_hash, _name) string(#_name),
static struct string hash_strings[] = {
    HASH_CODEC( DEFINE_ACTION )
    null_string
};
#undef DEFINE_ACTION

#define DEFINE_ACTION(_hash, _name) hash_##_name,
static hash_t hash_algos[] = {
    HASH_CODEC( DEFINE_ACTION )
    NULL
};
#undef DEFINE_ACTION

#define DEFINE_ACTION(_dist, _name) string(#_name),
static struct string dist_strings[] = {
    DIST_CODEC( DEFINE_ACTION )
    null_string
};
#undef DEFINE_ACTION

static struct command conf_commands[] = {
    { string("listen"),
      conf_set_listen,
      offsetof(struct conf_pool, listen) },

    { string("hash"),
      conf_set_hash,
      offsetof(struct conf_pool, hash) },

    { string("hash_tag"),
      conf_set_hashtag,
      offsetof(struct conf_pool, hash_tag) },

    { string("distribution"),
      conf_set_distribution,
      offsetof(struct conf_pool, distribution) },

    { string("timeout"),
      conf_set_num,
      offsetof(struct conf_pool, timeout) },

    { string("backlog"),
      conf_set_num,
      offsetof(struct conf_pool, backlog) },

    { string("client_connections"),
      conf_set_num,
      offsetof(struct conf_pool, client_connections) },

    { string("redis"),
      conf_set_bool,
      offsetof(struct conf_pool, redis) },

    { string("redis_auth"),
      conf_set_string,
      offsetof(struct conf_pool, redis_auth) },

    { string("preconnect"),
      conf_set_bool,
      offsetof(struct conf_pool, preconnect) },

    { string("auto_eject_hosts"),
      conf_set_bool,
      offsetof(struct conf_pool, auto_eject_hosts) },

    { string("server_connections"),
      conf_set_num,
      offsetof(struct conf_pool, server_connections) },

    { string("server_retry_timeout"),
      conf_set_num,
      offsetof(struct conf_pool, server_retry_timeout) },

    { string("server_failure_limit"),
      conf_set_num,
      offsetof(struct conf_pool, server_failure_limit) },

    { string("servers"),
      conf_add_server,
      offsetof(struct conf_pool, server) },

#if 1 //shenzheng 2015-6-5 tcpkeepalive
	{ string("tcpkeepalive"),
	  conf_set_bool,
	  offsetof(struct conf_pool, tcpkeepalive) },
	{ string("tcpkeepidle"),
	  conf_set_num,
	  offsetof(struct conf_pool, tcpkeepidle) },
	{ string("tcpkeepintvl"),
	  conf_set_num,
	  offsetof(struct conf_pool, tcpkeepintvl) },
	{ string("tcpkeepcnt"),
	  conf_set_num,
	  offsetof(struct conf_pool, tcpkeepcnt) },
#endif //shenzheng 2015-6-5 tcpkeepalive

    null_command
};

static void
conf_server_init(struct conf_server *cs)
{
    string_init(&cs->pname);
    string_init(&cs->name);
    cs->port = 0;
    cs->weight = 0;

    memset(&cs->info, 0, sizeof(cs->info));

    cs->valid = 0;

#if 1 //shenzheng 2014-9-5 replace server
	cs->name_null = 0;
#endif //shenzheng 2014-9-5 replace server

    log_debug(LOG_VVERB, "init conf server %p", cs);
}

static void
conf_server_deinit(struct conf_server *cs)
{
    string_deinit(&cs->pname);
    string_deinit(&cs->name);
    cs->valid = 0;
#if 1 //shenzheng 2014-9-5 replace server
	cs->name_null = 0;
#endif //shenzheng 2014-9-5 replace server
    log_debug(LOG_VVERB, "deinit conf server %p", cs);
}

rstatus_t
conf_server_each_transform(void *elem, void *data)
{
    struct conf_server *cs = elem;
    struct array *server = data;
    struct server *s;

    ASSERT(cs->valid);

    s = array_push(server);
    ASSERT(s != NULL);

    s->idx = array_idx(server, s);
    s->owner = NULL;

    s->pname = cs->pname;
    s->name = cs->name;
    s->port = (uint16_t)cs->port;
    s->weight = (uint32_t)cs->weight;

    s->family = cs->info.family;
    s->addrlen = cs->info.addrlen;
    s->addr = (struct sockaddr *)&cs->info.addr;

    s->ns_conn_q = 0;
    TAILQ_INIT(&s->s_conn_q);

    s->next_retry = 0LL;
    s->failure_count = 0;

#if 1 //shenzheng 2014-9-5 replace server
	s->name_null = cs->name_null;
#endif //shenzheng 2014-9-5 replace server

    log_debug(LOG_VERB, "transform to server %"PRIu32" '%.*s'",
              s->idx, s->pname.len, s->pname.data);

    return NC_OK;
}

static rstatus_t
conf_pool_init(struct conf_pool *cp, struct string *name)
{
    rstatus_t status;

    string_init(&cp->name);

    string_init(&cp->listen.pname);
    string_init(&cp->listen.name);
    string_init(&cp->redis_auth);
    cp->listen.port = 0;
    memset(&cp->listen.info, 0, sizeof(cp->listen.info));
    cp->listen.valid = 0;

    cp->hash = CONF_UNSET_HASH;
    string_init(&cp->hash_tag);
    cp->distribution = CONF_UNSET_DIST;

    cp->timeout = CONF_UNSET_NUM;
    cp->backlog = CONF_UNSET_NUM;

    cp->client_connections = CONF_UNSET_NUM;

    cp->redis = CONF_UNSET_NUM;
    cp->preconnect = CONF_UNSET_NUM;
    cp->auto_eject_hosts = CONF_UNSET_NUM;
    cp->server_connections = CONF_UNSET_NUM;
    cp->server_retry_timeout = CONF_UNSET_NUM;
    cp->server_failure_limit = CONF_UNSET_NUM;

    array_null(&cp->server);

    cp->valid = 0;

#if 1 //shenzheng 2015-6-5 tcpkeepalive
	cp->tcpkeepalive = CONF_UNSET_NUM;
	cp->tcpkeepidle = CONF_UNSET_NUM;
	cp->tcpkeepintvl = CONF_UNSET_NUM;
	cp->tcpkeepcnt = CONF_UNSET_NUM;
#endif //shenzheng 2015-6-5 tcpkeepalive

    status = string_duplicate(&cp->name, name);
    if (status != NC_OK) {
        return status;
    }

    status = array_init(&cp->server, CONF_DEFAULT_SERVERS,
                        sizeof(struct conf_server));
    if (status != NC_OK) {
        string_deinit(&cp->name);
        return status;
    }

    log_debug(LOG_VVERB, "init conf pool %p, '%.*s'", cp, name->len, name->data);

    return NC_OK;
}

static void
conf_pool_deinit(struct conf_pool *cp)
{
    string_deinit(&cp->name);

    string_deinit(&cp->listen.pname);
    string_deinit(&cp->listen.name);

    if (cp->redis_auth.len > 0) {
        string_deinit(&cp->redis_auth);
    }

    while (array_n(&cp->server) != 0) {
        conf_server_deinit(array_pop(&cp->server));
    }
    array_deinit(&cp->server);

    log_debug(LOG_VVERB, "deinit conf pool %p", cp);
}

rstatus_t
conf_pool_each_transform(void *elem, void *data)
{
    rstatus_t status;
    struct conf_pool *cp = elem;
    struct array *server_pool = data;
    struct server_pool *sp;

    ASSERT(cp->valid);

    sp = array_push(server_pool);
    ASSERT(sp != NULL);

    sp->idx = array_idx(server_pool, sp);
    sp->ctx = NULL;

    sp->p_conn = NULL;
    sp->nc_conn_q = 0;
    TAILQ_INIT(&sp->c_conn_q);

    array_null(&sp->server);
    sp->ncontinuum = 0;
    sp->nserver_continuum = 0;
    sp->continuum = NULL;
    sp->nlive_server = 0;
    sp->next_rebuild = 0LL;

    sp->name = cp->name;
    sp->addrstr = cp->listen.pname;
    sp->port = (uint16_t)cp->listen.port;

    sp->family = cp->listen.info.family;
    sp->addrlen = cp->listen.info.addrlen;
    sp->addr = (struct sockaddr *)&cp->listen.info.addr;

    sp->key_hash_type = cp->hash;
    sp->key_hash = hash_algos[cp->hash];
    sp->dist_type = cp->distribution;
    sp->hash_tag = cp->hash_tag;

    sp->redis = cp->redis ? 1 : 0;
    sp->redis_auth = cp->redis_auth;
    sp->timeout = cp->timeout;
    sp->backlog = cp->backlog;

    sp->client_connections = (uint32_t)cp->client_connections;

    sp->server_connections = (uint32_t)cp->server_connections;
    sp->server_retry_timeout = (int64_t)cp->server_retry_timeout * 1000LL;
    sp->server_failure_limit = (uint32_t)cp->server_failure_limit;
    sp->auto_eject_hosts = cp->auto_eject_hosts ? 1 : 0;
    sp->preconnect = cp->preconnect ? 1 : 0;

#if 1 //shenzheng 2015-6-5 tcpkeepalive
	sp->tcpkeepalive = cp->tcpkeepalive ? 1 : 0;
	sp->tcpkeepidle = cp->tcpkeepidle;
	sp->tcpkeepintvl = cp->tcpkeepintvl;
	sp->tcpkeepcnt = cp->tcpkeepcnt;
#endif //shenzheng 2015-6-5 tcpkeepalive

    status = server_init(&sp->server, &cp->server, sp);
    if (status != NC_OK) {
        return status;
    }

    log_debug(LOG_VERB, "transform to pool %"PRIu32" '%.*s'", sp->idx,
              sp->name.len, sp->name.data);

    return NC_OK;
}

static void
conf_dump(struct conf *cf)
{
    uint32_t i, j, npool, nserver;
    struct conf_pool *cp;
    struct string *s;

    npool = array_n(&cf->pool);
    if (npool == 0) {
        return;
    }

    log_debug(LOG_VVERB, "%"PRIu32" pools in configuration file '%s'", npool,
              cf->fname);

    for (i = 0; i < npool; i++) {
        cp = array_get(&cf->pool, i);

        log_debug(LOG_VVERB, "%.*s", cp->name.len, cp->name.data);
        log_debug(LOG_VVERB, "  listen: %.*s",
                  cp->listen.pname.len, cp->listen.pname.data);
        log_debug(LOG_VVERB, "  timeout: %d", cp->timeout);
        log_debug(LOG_VVERB, "  backlog: %d", cp->backlog);
        log_debug(LOG_VVERB, "  hash: %d", cp->hash);
        log_debug(LOG_VVERB, "  hash_tag: \"%.*s\"", cp->hash_tag.len,
                  cp->hash_tag.data);
        log_debug(LOG_VVERB, "  distribution: %d", cp->distribution);
        log_debug(LOG_VVERB, "  client_connections: %d",
                  cp->client_connections);
        log_debug(LOG_VVERB, "  redis: %d", cp->redis);
        log_debug(LOG_VVERB, "  preconnect: %d", cp->preconnect);
        log_debug(LOG_VVERB, "  auto_eject_hosts: %d", cp->auto_eject_hosts);
        log_debug(LOG_VVERB, "  server_connections: %d",
                  cp->server_connections);
        log_debug(LOG_VVERB, "  server_retry_timeout: %d",
                  cp->server_retry_timeout);
        log_debug(LOG_VVERB, "  server_failure_limit: %d",
                  cp->server_failure_limit);

        nserver = array_n(&cp->server);
        log_debug(LOG_VVERB, "  servers: %"PRIu32"", nserver);

        for (j = 0; j < nserver; j++) {
            s = array_get(&cp->server, j);
            log_debug(LOG_VVERB, "    %.*s", s->len, s->data);
        }
    }
}

static rstatus_t
conf_yaml_init(struct conf *cf)
{
    int rv;

    ASSERT(!cf->valid_parser);

    rv = fseek(cf->fh, 0L, SEEK_SET);
    if (rv < 0) {
        log_error("conf: failed to seek to the beginning of file '%s': %s",
                  cf->fname, strerror(errno));
        return NC_ERROR;
    }

    rv = yaml_parser_initialize(&cf->parser);
    if (!rv) {
        log_error("conf: failed (err %d) to initialize yaml parser",
                  cf->parser.error);
        return NC_ERROR;
    }

    yaml_parser_set_input_file(&cf->parser, cf->fh);
    cf->valid_parser = 1;

    return NC_OK;
}

static void
conf_yaml_deinit(struct conf *cf)
{
    if (cf->valid_parser) {
        yaml_parser_delete(&cf->parser);
        cf->valid_parser = 0;
    }
}

static rstatus_t
conf_token_next(struct conf *cf)
{
    int rv;

    ASSERT(cf->valid_parser && !cf->valid_token);

    rv = yaml_parser_scan(&cf->parser, &cf->token);
    if (!rv) {
        log_error("conf: failed (err %d) to scan next token", cf->parser.error);
        return NC_ERROR;
    }
    cf->valid_token = 1;

    return NC_OK;
}

static void
conf_token_done(struct conf *cf)
{
    ASSERT(cf->valid_parser);

    if (cf->valid_token) {
        yaml_token_delete(&cf->token);
        cf->valid_token = 0;
    }
}

static rstatus_t
conf_event_next(struct conf *cf)
{
    int rv;

    ASSERT(cf->valid_parser && !cf->valid_event);

    rv = yaml_parser_parse(&cf->parser, &cf->event);
    if (!rv) {
        log_error("conf: failed (err %d) to get next event", cf->parser.error);
        return NC_ERROR;
    }
    cf->valid_event = 1;

    return NC_OK;
}

static void
conf_event_done(struct conf *cf)
{
    if (cf->valid_event) {
        yaml_event_delete(&cf->event);
        cf->valid_event = 0;
    }
}

static rstatus_t
conf_push_scalar(struct conf *cf)
{
    rstatus_t status;
    struct string *value;
    uint8_t *scalar;
    uint32_t scalar_len;

    scalar = cf->event.data.scalar.value;
    scalar_len = (uint32_t)cf->event.data.scalar.length;

#if 1 //shenzheng 2015-6-24 fix bug: if conf value is null, core dump 
	if(scalar_len == 0)
	{
		return NC_ERROR;
	}
#endif //shenzheng 2015-6-24 fix bug: if conf value is null, core dump 

    log_debug(LOG_VVERB, "push '%.*s'", scalar_len, scalar);

    value = array_push(&cf->arg);
    if (value == NULL) {
        return NC_ENOMEM;
    }
    string_init(value);

    status = string_copy(value, scalar, scalar_len);
    if (status != NC_OK) {
        array_pop(&cf->arg);
        return status;
    }

    return NC_OK;
}

static void
conf_pop_scalar(struct conf *cf)
{
    struct string *value;

    value = array_pop(&cf->arg);
    log_debug(LOG_VVERB, "pop '%.*s'", value->len, value->data);
    string_deinit(value);
}

static rstatus_t
conf_handler(struct conf *cf, void *data)
{
    struct command *cmd;
    struct string *key, *value;
    uint32_t narg;

    if (array_n(&cf->arg) == 1) {
        value = array_top(&cf->arg);
        log_debug(LOG_VVERB, "conf handler on '%.*s'", value->len, value->data);
        return conf_pool_init(data, value);
    }

    narg = array_n(&cf->arg);
    value = array_get(&cf->arg, narg - 1);
    key = array_get(&cf->arg, narg - 2);

    log_debug(LOG_VVERB, "conf handler on %.*s: %.*s", key->len, key->data,
              value->len, value->data);

    for (cmd = conf_commands; cmd->name.len != 0; cmd++) {
        char *rv;

        if (string_compare(key, &cmd->name) != 0) {
            continue;
        }

        rv = cmd->set(cf, cmd, data);
        if (rv != CONF_OK) {
            log_error("conf: directive \"%.*s\" %s", key->len, key->data, rv);
            return NC_ERROR;
        }

        return NC_OK;
    }

    log_error("conf: directive \"%.*s\" is unknown", key->len, key->data);

    return NC_ERROR;
}

static rstatus_t
conf_begin_parse(struct conf *cf)
{
    rstatus_t status;
    bool done;

    ASSERT(cf->sound && !cf->parsed);
    ASSERT(cf->depth == 0);

    status = conf_yaml_init(cf);
    if (status != NC_OK) {
        return status;
    }

    done = false;
    do {
        status = conf_event_next(cf);
        if (status != NC_OK) {
            return status;
        }

        log_debug(LOG_VVERB, "next begin event %d", cf->event.type);

        switch (cf->event.type) {
        case YAML_STREAM_START_EVENT:
        case YAML_DOCUMENT_START_EVENT:
            break;

        case YAML_MAPPING_START_EVENT:
            ASSERT(cf->depth < CONF_MAX_DEPTH);
            cf->depth++;
            done = true;
            break;

        default:
#if 1 //shenzheng 2015-7-15 config-reload
			/* to avoid twemproxy core dump
		  	  * just when reload_conf and conf 
		  	  * file modified by others  */
			conf_event_done(cf);
			return NC_ERROR;		
#else //shenzheng 2015-7-15 config-reload
            NOT_REACHED();
#endif //shenzheng 2015-7-15 config-reload
        }

        conf_event_done(cf);

    } while (!done);

    return NC_OK;
}

static rstatus_t
conf_end_parse(struct conf *cf)
{
    rstatus_t status;
    bool done;

    ASSERT(cf->sound && !cf->parsed);
    ASSERT(cf->depth == 0);

    done = false;
    do {
        status = conf_event_next(cf);
        if (status != NC_OK) {
            return status;
        }

        log_debug(LOG_VVERB, "next end event %d", cf->event.type);

        switch (cf->event.type) {
        case YAML_STREAM_END_EVENT:
            done = true;
            break;

        case YAML_DOCUMENT_END_EVENT:
            break;

        default:
#if 1 //shenzheng 2015-7-15 config-reload
			/* to avoid twemproxy core dump
		 	  * just when reload_conf and conf 
		  	  * file modified by others  */
			conf_event_done(cf);
			conf_yaml_deinit(cf);
			return NC_ERROR;		
#else //shenzheng 2015-7-15 config-reload
            NOT_REACHED();
#endif //shenzheng 2015-7-15 config-reload
        }

        conf_event_done(cf);
    } while (!done);

    conf_yaml_deinit(cf);

    return NC_OK;
}

static rstatus_t
conf_parse_core(struct conf *cf, void *data)
{
    rstatus_t status;
    bool done, leaf, new_pool;

    ASSERT(cf->sound);

    status = conf_event_next(cf);
    if (status != NC_OK) {
        return status;
    }

    log_debug(LOG_VVERB, "next event %d depth %"PRIu32" seq %d", cf->event.type,
              cf->depth, cf->seq);

    done = false;
    leaf = false;
    new_pool = false;

    switch (cf->event.type) {
    case YAML_MAPPING_END_EVENT:
        cf->depth--;
        if (cf->depth == 1) {
            conf_pop_scalar(cf);
        } else if (cf->depth == 0) {
            done = true;
        }
        break;

    case YAML_MAPPING_START_EVENT:
        cf->depth++;
        break;

    case YAML_SEQUENCE_START_EVENT:
        cf->seq = 1;
        break;

    case YAML_SEQUENCE_END_EVENT:
        conf_pop_scalar(cf);
        cf->seq = 0;
        break;

    case YAML_SCALAR_EVENT:
        status = conf_push_scalar(cf);
        if (status != NC_OK) {
            break;
        }

        /* take appropriate action */
        if (cf->seq) {
            /* for a sequence, leaf is at CONF_MAX_DEPTH */
#if 1 //shenzheng 2015-7-15 config-reload
			/* to avoid twemproxy core dump
		  	  * just when reload_conf and conf 
		  	  * file modified by others  */
			if(cf->depth != CONF_MAX_DEPTH)
			{
				status = NC_ERROR;
				break;
			}
			else
			{
#endif //shenzheng 2015-7-15 config-reload
            ASSERT(cf->depth == CONF_MAX_DEPTH);
#if 1 //shenzheng 2015-7-15 config-reload
			}
#endif //shenzheng 2015-7-15 config-reload
            leaf = true;
        } else if (cf->depth == CONF_ROOT_DEPTH) {
            /* create new conf_pool */
            data = array_push(&cf->pool);
            if (data == NULL) {
                status = NC_ENOMEM;
                break;
           }
			
#if 1 //shenzheng 2015-7-15 config-reload
			/* add those init to avoid twemproxy core dump
			  * just when reload_conf and conf file modified by others  */
			struct conf_pool *cp_tmp = data;
			string_init(&cp_tmp->name);
			string_init(&cp_tmp->listen.pname);
			string_init(&cp_tmp->listen.name);
			string_init(&cp_tmp->redis_auth);
			string_init(&cp_tmp->hash_tag);
			array_null(&cp_tmp->server);
#endif //shenzheng 2015-7-15 config-reload

           new_pool = true;
        } else if (array_n(&cf->arg) == cf->depth + 1) {
            /* for {key: value}, leaf is at CONF_MAX_DEPTH */
            ASSERT(cf->depth == CONF_MAX_DEPTH);
            leaf = true;
        }
        break;

    default:
#if 1 //shenzheng 2015-7-15 config-reload
		/* to avoid twemproxy core dump
		  * just when reload_conf and conf 
		  * file modified by others  */
		status = NC_ERROR;		
#else //shenzheng 2015-7-15 config-reload
        NOT_REACHED();
#endif //shenzheng 2015-7-15 config-reload
        break;
    }

    conf_event_done(cf);

    if (status != NC_OK) {
        return status;
    }

    if (done) {
        /* terminating condition */
        return NC_OK;
    }

    if (leaf || new_pool) {
        status = conf_handler(cf, data);

        if (leaf) {
            conf_pop_scalar(cf);
            if (!cf->seq) {
                conf_pop_scalar(cf);
            }
        }

        if (status != NC_OK) {
            return status;
        }
    }

    return conf_parse_core(cf, data);
}

static rstatus_t
conf_parse(struct conf *cf)
{
    rstatus_t status;

    ASSERT(cf->sound && !cf->parsed);
    ASSERT(array_n(&cf->arg) == 0);

    status = conf_begin_parse(cf);
    if (status != NC_OK) {
        return status;
    }

    status = conf_parse_core(cf, NULL);
    if (status != NC_OK) {
        return status;
    }

    status = conf_end_parse(cf);
    if (status != NC_OK) {
        return status;
    }

    cf->parsed = 1;

    return NC_OK;
}

static struct conf *
conf_open(char *filename)
{
    rstatus_t status;
    struct conf *cf;
    FILE *fh;

    fh = fopen(filename, "r");
    if (fh == NULL) {
        log_error("conf: failed to open configuration '%s': %s", filename,
                  strerror(errno));
        return NULL;
    }

    cf = nc_alloc(sizeof(*cf));
    if (cf == NULL) {
        fclose(fh);
        return NULL;
    }

    status = array_init(&cf->arg, CONF_DEFAULT_ARGS, sizeof(struct string));
    if (status != NC_OK) {
        nc_free(cf);
        fclose(fh);
        return NULL;
    }

    status = array_init(&cf->pool, CONF_DEFAULT_POOL, sizeof(struct conf_pool));
    if (status != NC_OK) {
        array_deinit(&cf->arg);
        nc_free(cf);
        fclose(fh);
        return NULL;
    }

    cf->fname = filename;
    cf->fh = fh;
    cf->depth = 0;
    /* parser, event, and token are initialized later */
    cf->seq = 0;
    cf->valid_parser = 0;
    cf->valid_event = 0;
    cf->valid_token = 0;
    cf->sound = 0;
    cf->parsed = 0;
    cf->valid = 0;

    log_debug(LOG_VVERB, "opened conf '%s'", filename);

    return cf;
}

static rstatus_t
conf_validate_document(struct conf *cf)
{
    rstatus_t status;
    uint32_t count;
    bool done;

    status = conf_yaml_init(cf);
    if (status != NC_OK) {
        return status;
    }

    count = 0;
    done = false;
    do {
        yaml_document_t document;
        yaml_node_t *node;
        int rv;

        rv = yaml_parser_load(&cf->parser, &document);
        if (!rv) {
            log_error("conf: failed (err %d) to get the next yaml document",
                      cf->parser.error);
            conf_yaml_deinit(cf);
            return NC_ERROR;
        }

        node = yaml_document_get_root_node(&document);
        if (node == NULL) {
            done = true;
        } else {
            count++;
        }

        yaml_document_delete(&document);
    } while (!done);

    conf_yaml_deinit(cf);

    if (count != 1) {
        log_error("conf: '%s' must contain only 1 document; found %"PRIu32" "
                  "documents", cf->fname, count);
        return NC_ERROR;
    }

    return NC_OK;
}

static rstatus_t
conf_validate_tokens(struct conf *cf)
{
    rstatus_t status;
    bool done, error;
    int type;

    status = conf_yaml_init(cf);
    if (status != NC_OK) {
        return status;
    }

    done = false;
    error = false;
    do {
        status = conf_token_next(cf);
        if (status != NC_OK) {
            return status;
        }
        type = cf->token.type;

        switch (type) {
        case YAML_NO_TOKEN:
            error = true;
            log_error("conf: no token (%d) is disallowed", type);
            break;

        case YAML_VERSION_DIRECTIVE_TOKEN:
            error = true;
            log_error("conf: version directive token (%d) is disallowed", type);
            break;

        case YAML_TAG_DIRECTIVE_TOKEN:
            error = true;
            log_error("conf: tag directive token (%d) is disallowed", type);
            break;

        case YAML_DOCUMENT_START_TOKEN:
            error = true;
            log_error("conf: document start token (%d) is disallowed", type);
            break;

        case YAML_DOCUMENT_END_TOKEN:
            error = true;
            log_error("conf: document end token (%d) is disallowed", type);
            break;

        case YAML_FLOW_SEQUENCE_START_TOKEN:
            error = true;
            log_error("conf: flow sequence start token (%d) is disallowed", type);
            break;

        case YAML_FLOW_SEQUENCE_END_TOKEN:
            error = true;
            log_error("conf: flow sequence end token (%d) is disallowed", type);
            break;

        case YAML_FLOW_MAPPING_START_TOKEN:
            error = true;
            log_error("conf: flow mapping start token (%d) is disallowed", type);
            break;

        case YAML_FLOW_MAPPING_END_TOKEN:
            error = true;
            log_error("conf: flow mapping end token (%d) is disallowed", type);
            break;

        case YAML_FLOW_ENTRY_TOKEN:
            error = true;
            log_error("conf: flow entry token (%d) is disallowed", type);
            break;

        case YAML_ALIAS_TOKEN:
            error = true;
            log_error("conf: alias token (%d) is disallowed", type);
            break;

        case YAML_ANCHOR_TOKEN:
            error = true;
            log_error("conf: anchor token (%d) is disallowed", type);
            break;

        case YAML_TAG_TOKEN:
            error = true;
            log_error("conf: tag token (%d) is disallowed", type);
            break;

        case YAML_BLOCK_SEQUENCE_START_TOKEN:
        case YAML_BLOCK_MAPPING_START_TOKEN:
        case YAML_BLOCK_END_TOKEN:
        case YAML_BLOCK_ENTRY_TOKEN:
            break;

        case YAML_KEY_TOKEN:
        case YAML_VALUE_TOKEN:
        case YAML_SCALAR_TOKEN:
            break;

        case YAML_STREAM_START_TOKEN:
            break;

        case YAML_STREAM_END_TOKEN:
            done = true;
            log_debug(LOG_VVERB, "conf '%s' has valid tokens", cf->fname);
            break;

        default:
            error = true;
            log_error("conf: unknown token (%d) is disallowed", type);
            break;
        }

        conf_token_done(cf);
    } while (!done && !error);

    conf_yaml_deinit(cf);

    return !error ? NC_OK : NC_ERROR;
}

static rstatus_t
conf_validate_structure(struct conf *cf)
{
    rstatus_t status;
    int type, depth;
    uint32_t i, count[CONF_MAX_DEPTH + 1];
    bool done, error, seq;

    status = conf_yaml_init(cf);
    if (status != NC_OK) {
        return status;
    }

    done = false;
    error = false;
    seq = false;
    depth = 0;
    for (i = 0; i < CONF_MAX_DEPTH + 1; i++) {
        count[i] = 0;
    }

    /*
     * Validate that the configuration conforms roughly to the following
     * yaml tree structure:
     *
     * keyx:
     *   key1: value1
     *   key2: value2
     *   seq:
     *     - elem1
     *     - elem2
     *     - elem3
     *   key3: value3
     *
     * keyy:
     *   key1: value1
     *   key2: value2
     *   seq:
     *     - elem1
     *     - elem2
     *     - elem3
     *   key3: value3
     */
    do {
        status = conf_event_next(cf);
        if (status != NC_OK) {
            return status;
        }

        type = cf->event.type;

        log_debug(LOG_VVERB, "next event %d depth %d seq %d", type, depth, seq);

        switch (type) {
        case YAML_STREAM_START_EVENT:
        case YAML_DOCUMENT_START_EVENT:
            break;

        case YAML_DOCUMENT_END_EVENT:
            break;

        case YAML_STREAM_END_EVENT:
            done = true;
            break;

        case YAML_MAPPING_START_EVENT:
            if (depth == CONF_ROOT_DEPTH && count[depth] != 1) {
                error = true;
                log_error("conf: '%s' has more than one \"key:value\" at depth"
                          " %d", cf->fname, depth);
            } else if (depth >= CONF_MAX_DEPTH) {
                error = true;
                log_error("conf: '%s' has a depth greater than %d", cf->fname,
                          CONF_MAX_DEPTH);
            }
            depth++;
            break;

        case YAML_MAPPING_END_EVENT:
            if (depth == CONF_MAX_DEPTH) {
                if (seq) {
                    seq = false;
                } else {
                    error = true;
                    log_error("conf: '%s' missing sequence directive at depth "
                              "%d", cf->fname, depth);
                }
            }
            depth--;
            count[depth] = 0;
            break;

        case YAML_SEQUENCE_START_EVENT:
            if (seq) {
                error = true;
                log_error("conf: '%s' has more than one sequence directive",
                          cf->fname);
            } else if (depth != CONF_MAX_DEPTH) {
                error = true;
                log_error("conf: '%s' has sequence at depth %d instead of %d",
                          cf->fname, depth, CONF_MAX_DEPTH);
            } else if (count[depth] != 1) {
                error = true;
                log_error("conf: '%s' has invalid \"key:value\" at depth %d",
                          cf->fname, depth);
            }
            seq = true;
            break;

        case YAML_SEQUENCE_END_EVENT:
            ASSERT(depth == CONF_MAX_DEPTH);
            count[depth] = 0;
            break;

        case YAML_SCALAR_EVENT:
            if (depth == 0) {
                error = true;
                log_error("conf: '%s' has invalid empty \"key:\" at depth %d",
                          cf->fname, depth);
            } else if (depth == CONF_ROOT_DEPTH && count[depth] != 0) {
                error = true;
                log_error("conf: '%s' has invalid mapping \"key:\" at depth %d",
                          cf->fname, depth);
            } else if (depth == CONF_MAX_DEPTH && count[depth] == 2) {
                /* found a "key: value", resetting! */
                count[depth] = 0;
            }
            count[depth]++;
            break;

        default:
            NOT_REACHED();
        }

        conf_event_done(cf);
    } while (!done && !error);

    conf_yaml_deinit(cf);

    return !error ? NC_OK : NC_ERROR;
}

static rstatus_t
conf_pre_validate(struct conf *cf)
{
    rstatus_t status;

    status = conf_validate_document(cf);
    if (status != NC_OK) {
        return status;
    }

    status = conf_validate_tokens(cf);
    if (status != NC_OK) {
        return status;
    }

    status = conf_validate_structure(cf);
    if (status != NC_OK) {
        return status;
    }

    cf->sound = 1;

    return NC_OK;
}

static int
conf_server_name_cmp(const void *t1, const void *t2)
{
    const struct conf_server *s1 = t1, *s2 = t2;

    return string_compare(&s1->name, &s2->name);
}

static int
conf_pool_name_cmp(const void *t1, const void *t2)
{
    const struct conf_pool *p1 = t1, *p2 = t2;

    return string_compare(&p1->name, &p2->name);
}

static int
conf_pool_listen_cmp(const void *t1, const void *t2)
{
    const struct conf_pool *p1 = t1, *p2 = t2;

    return string_compare(&p1->listen.pname, &p2->listen.pname);
}

static rstatus_t
conf_validate_server(struct conf *cf, struct conf_pool *cp)
{
    uint32_t i, nserver;
    bool valid;

    nserver = array_n(&cp->server);
    if (nserver == 0) {
        log_error("conf: pool '%.*s' has no servers", cp->name.len,
                  cp->name.data);
        return NC_ERROR;
    }

    /*
     * Disallow duplicate servers - servers with identical "host:port:weight"
     * or "name" combination are considered as duplicates. When server name
     * is configured, we only check for duplicate "name" and not for duplicate
     * "host:port:weight"
     */
    array_sort(&cp->server, conf_server_name_cmp);
    for (valid = true, i = 0; i < nserver - 1; i++) {
        struct conf_server *cs1, *cs2;

        cs1 = array_get(&cp->server, i);
        cs2 = array_get(&cp->server, i + 1);

        if (string_compare(&cs1->name, &cs2->name) == 0) {
            log_error("conf: pool '%.*s' has servers with same name '%.*s'",
                      cp->name.len, cp->name.data, cs1->name.len, 
                      cs1->name.data);
            valid = false;
            break;
        }
    }
    if (!valid) {
        return NC_ERROR;
    }

    return NC_OK;
}

static rstatus_t
conf_validate_pool(struct conf *cf, struct conf_pool *cp)
{
    rstatus_t status;

    ASSERT(!cp->valid);
    ASSERT(!string_empty(&cp->name));

    if (!cp->listen.valid) {
        log_error("conf: directive \"listen:\" is missing");
        return NC_ERROR;
    }

    /* set default values for unset directives */

    if (cp->distribution == CONF_UNSET_DIST) {
        cp->distribution = CONF_DEFAULT_DIST;
    }

    if (cp->hash == CONF_UNSET_HASH) {
        cp->hash = CONF_DEFAULT_HASH;
    }

    if (cp->timeout == CONF_UNSET_NUM) {
        cp->timeout = CONF_DEFAULT_TIMEOUT;
    }

    if (cp->backlog == CONF_UNSET_NUM) {
        cp->backlog = CONF_DEFAULT_LISTEN_BACKLOG;
    }

    cp->client_connections = CONF_DEFAULT_CLIENT_CONNECTIONS;

    if (cp->redis == CONF_UNSET_NUM) {
        cp->redis = CONF_DEFAULT_REDIS;
    }

    if (cp->preconnect == CONF_UNSET_NUM) {
        cp->preconnect = CONF_DEFAULT_PRECONNECT;
    }

    if (cp->auto_eject_hosts == CONF_UNSET_NUM) {
        cp->auto_eject_hosts = CONF_DEFAULT_AUTO_EJECT_HOSTS;
    }

    if (cp->server_connections == CONF_UNSET_NUM) {
        cp->server_connections = CONF_DEFAULT_SERVER_CONNECTIONS;
    } else if (cp->server_connections == 0) {
        log_error("conf: directive \"server_connections:\" cannot be 0");
        return NC_ERROR;
    }

    if (cp->server_retry_timeout == CONF_UNSET_NUM) {
        cp->server_retry_timeout = CONF_DEFAULT_SERVER_RETRY_TIMEOUT;
    }

    if (cp->server_failure_limit == CONF_UNSET_NUM) {
        cp->server_failure_limit = CONF_DEFAULT_SERVER_FAILURE_LIMIT;
    }

#if 1 //shenzheng 2015-6-5 tcpkeepalive
	if (cp->tcpkeepalive == CONF_UNSET_NUM) {
		cp->tcpkeepalive = CONF_DEFAULT_TCPKEEPALIVE;
	}
	if (cp->tcpkeepidle == CONF_UNSET_NUM) {
		cp->tcpkeepidle = CONF_DEFAULT_TCPKEEPIDLE;
	}
	if (cp->tcpkeepintvl == CONF_UNSET_NUM) {
		cp->tcpkeepintvl = CONF_DEFAULT_TCPKEEPINTVL;
	}
	if (cp->tcpkeepcnt == CONF_UNSET_NUM) {
		cp->tcpkeepcnt = CONF_DEFAULT_TCPKEEPCNT;
	}
#endif //shenzheng 2015-6-5 tcpkeepalive

    status = conf_validate_server(cf, cp);
    if (status != NC_OK) {
        return status;
    }

    cp->valid = 1;

    return NC_OK;
}

static rstatus_t
conf_post_validate(struct conf *cf)
{
    rstatus_t status;
    uint32_t i, npool;
    bool valid;

    ASSERT(cf->sound && cf->parsed);
    ASSERT(!cf->valid);

    npool = array_n(&cf->pool);
    if (npool == 0) {
        log_error("conf: '%.*s' has no pools", cf->fname);
        return NC_ERROR;
    }

    /* validate pool */
    for (i = 0; i < npool; i++) {
        struct conf_pool *cp = array_get(&cf->pool, i);

        status = conf_validate_pool(cf, cp);
        if (status != NC_OK) {
            return status;
        }
    }

    /* disallow pools with duplicate listen: key values */
    array_sort(&cf->pool, conf_pool_listen_cmp);
    for (valid = true, i = 0; i < npool - 1; i++) {
        struct conf_pool *p1, *p2;

        p1 = array_get(&cf->pool, i);
        p2 = array_get(&cf->pool, i + 1);

        if (string_compare(&p1->listen.pname, &p2->listen.pname) == 0) {
            log_error("conf: pools '%.*s' and '%.*s' have the same listen "
                      "address '%.*s'", p1->name.len, p1->name.data,
                      p2->name.len, p2->name.data, p1->listen.pname.len,
                      p1->listen.pname.data);
            valid = false;
            break;
        }
    }
    if (!valid) {
        return NC_ERROR;
    }

    /* disallow pools with duplicate names */
    array_sort(&cf->pool, conf_pool_name_cmp);
    for (valid = true, i = 0; i < npool - 1; i++) {
        struct conf_pool *p1, *p2;

        p1 = array_get(&cf->pool, i);
        p2 = array_get(&cf->pool, i + 1);

        if (string_compare(&p1->name, &p2->name) == 0) {
            log_error("conf: '%s' has pools with same name %.*s'", cf->fname,
                      p1->name.len, p1->name.data);
            valid = false;
            break;
        }
    }
    if (!valid) {
        return NC_ERROR;
    }

    return NC_OK;
}

struct conf *
conf_create(char *filename)
{
    rstatus_t status;
    struct conf *cf;

    cf = conf_open(filename);
    if (cf == NULL) {
        return NULL;
    }

    /* validate configuration file before parsing */
    status = conf_pre_validate(cf);
    if (status != NC_OK) {
        goto error;
    }

    /* parse the configuration file */
    status = conf_parse(cf);
    if (status != NC_OK) {
        goto error;
    }

    /* validate parsed configuration */
    status = conf_post_validate(cf);
    if (status != NC_OK) {
        goto error;
    }

    conf_dump(cf);

    fclose(cf->fh);
    cf->fh = NULL;

    return cf;

error:
    fclose(cf->fh);
    cf->fh = NULL;
    conf_destroy(cf);
    return NULL;
}

void
conf_destroy(struct conf *cf)
{
    while (array_n(&cf->arg) != 0) {
        conf_pop_scalar(cf);
    }
    array_deinit(&cf->arg);

    while (array_n(&cf->pool) != 0) {
        conf_pool_deinit(array_pop(&cf->pool));
    }
    array_deinit(&cf->pool);

    nc_free(cf);
}

char *
conf_set_string(struct conf *cf, struct command *cmd, void *conf)
{
    rstatus_t status;
    uint8_t *p;
    struct string *field, *value;

    p = conf;
    field = (struct string *)(p + cmd->offset);

    if (field->data != CONF_UNSET_PTR) {
        return "is a duplicate";
    }

    value = array_top(&cf->arg);

    status = string_duplicate(field, value);
    if (status != NC_OK) {
        return CONF_ERROR;
    }

    return CONF_OK;
}

char *
conf_set_listen(struct conf *cf, struct command *cmd, void *conf)
{
    rstatus_t status;
    struct string *value;
    struct conf_listen *field;
    uint8_t *p, *name;
    uint32_t namelen;

    p = conf;
    field = (struct conf_listen *)(p + cmd->offset);

    if (field->valid == 1) {
        return "is a duplicate";
    }

    value = array_top(&cf->arg);

    status = string_duplicate(&field->pname, value);
    if (status != NC_OK) {
        return CONF_ERROR;
    }

    if (value->data[0] == '/') {
        name = value->data;
        namelen = value->len;
    } else {
        uint8_t *q, *start, *port;
        uint32_t portlen;

        /* parse "hostname:port" from the end */
        p = value->data + value->len - 1;
        start = value->data;
        q = nc_strrchr(p, start, ':');
        if (q == NULL) {
            return "has an invalid \"hostname:port\" format string";
        }

        port = q + 1;
        portlen = (uint32_t)(p - port + 1);

        p = q - 1;

        name = start;
        namelen = (uint32_t)(p - start + 1);

        field->port = nc_atoi(port, portlen);
        if (field->port < 0 || !nc_valid_port(field->port)) {
            return "has an invalid port in \"hostname:port\" format string";
        }
    }

    status = string_copy(&field->name, name, namelen);
    if (status != NC_OK) {
        return CONF_ERROR;
    }

    status = nc_resolve(&field->name, field->port, &field->info);
    if (status != NC_OK) {
        return CONF_ERROR;
    }

    field->valid = 1;

    return CONF_OK;
}

char *
conf_add_server(struct conf *cf, struct command *cmd, void *conf)
{
    rstatus_t status;
    struct array *a;
    struct string *value;
    struct conf_server *field;
    uint8_t *p, *q, *start;
    uint8_t *pname, *addr, *port, *weight, *name;
    uint32_t k, delimlen, pnamelen, addrlen, portlen, weightlen, namelen;
    struct string address;
    char delim[] = " ::";

    string_init(&address);
    p = conf;
    a = (struct array *)(p + cmd->offset);

    field = array_push(a);
    if (field == NULL) {
        return CONF_ERROR;
    }

    conf_server_init(field);

    value = array_top(&cf->arg);

    /* parse "hostname:port:weight [name]" or "/path/unix_socket:weight [name]" from the end */
    p = value->data + value->len - 1;
    start = value->data;
    addr = NULL;
    addrlen = 0;
    weight = NULL;
    weightlen = 0;
    port = NULL;
    portlen = 0;
    name = NULL;
    namelen = 0;

    delimlen = value->data[0] == '/' ? 2 : 3;

	//parse from the end of the string, p: the end position of remained string that will be parsed, 
	//start: the begin position of the string, q: the begin position of the part string that has parsed.
    for (k = 0; k < sizeof(delim); k++) {
        q = nc_strrchr(p, start, delim[k]);
        if (q == NULL) {
            if (k == 0) {
                /*
                 * name in "hostname:port:weight [name]" format string is
                 * optional
                 */
                continue;
            }
            break;
        }

        switch (k) {
        case 0:
            name = q + 1;
            namelen = (uint32_t)(p - name + 1);
            break;

        case 1:
            weight = q + 1;
            weightlen = (uint32_t)(p - weight + 1);
            break;

        case 2:
            port = q + 1;
            portlen = (uint32_t)(p - port + 1);
            break;

        default:
            NOT_REACHED();
        }

        p = q - 1;
    }

    if (k != delimlen) {
        return "has an invalid \"hostname:port:weight [name]\"or \"/path/unix_socket:weight [name]\" format string";
    }

    pname = value->data;
    pnamelen = namelen > 0 ? value->len - (namelen + 1) : value->len;
    status = string_copy(&field->pname, pname, pnamelen);
    if (status != NC_OK) {
        array_pop(a);
        return CONF_ERROR;
    }

    addr = start;
    addrlen = (uint32_t)(p - start + 1);

    field->weight = nc_atoi(weight, weightlen);
    if (field->weight < 0) {
        return "has an invalid weight in \"hostname:port:weight [name]\" format string";
    } else if (field->weight == 0) {
        return "has a zero weight in \"hostname:port:weight [name]\" format string";
    }

    if (value->data[0] != '/') {
        field->port = nc_atoi(port, portlen);
        if (field->port < 0 || !nc_valid_port(field->port)) {
            return "has an invalid port in \"hostname:port:weight [name]\" format string";
        }
    }

    if (name == NULL) {
        /*
         * To maintain backward compatibility with libmemcached, we don't
         * include the port as the part of the input string to the consistent
         * hashing algorithm, when it is equal to 11211.
         */
        if (field->port == CONF_DEFAULT_KETAMA_PORT) {
            name = addr;
            namelen = addrlen;
        } else {
            name = addr;
            namelen = addrlen + 1 + portlen;
        }

#if 1 //shenzheng 2014-9-5 replace server
		field->name_null = 1;
#endif //shenzheng 2014-9-5 replace server

    }

    status = string_copy(&field->name, name, namelen);
    if (status != NC_OK) {
        return CONF_ERROR;
    }

    status = string_copy(&address, addr, addrlen);	//the ip string
    if (status != NC_OK) {
        return CONF_ERROR;
    }

    status = nc_resolve(&address, field->port, &field->info);
    if (status != NC_OK) {
        string_deinit(&address);
        return CONF_ERROR;
    }

    string_deinit(&address);
    field->valid = 1;

    return CONF_OK;
}

char *
conf_set_num(struct conf *cf, struct command *cmd, void *conf)
{
    uint8_t *p;
    int num, *np;
    struct string *value;

    p = conf;
    np = (int *)(p + cmd->offset);

    if (*np != CONF_UNSET_NUM) {
        return "is a duplicate";
    }

    value = array_top(&cf->arg);

    num = nc_atoi(value->data, value->len);
    if (num < 0) {
        return "is not a number";
    }

    *np = num;

    return CONF_OK;
}

char *
conf_set_bool(struct conf *cf, struct command *cmd, void *conf)
{
    uint8_t *p;
    int *bp;
    struct string *value, true_str, false_str;

    p = conf;
    bp = (int *)(p + cmd->offset);

    if (*bp != CONF_UNSET_NUM) {
        return "is a duplicate";
    }

    value = array_top(&cf->arg);
    string_set_text(&true_str, "true");
    string_set_text(&false_str, "false");

    if (string_compare(value, &true_str) == 0) {
        *bp = 1;
    } else if (string_compare(value, &false_str) == 0) {
        *bp = 0;
    } else {
        return "is not \"true\" or \"false\"";
    }

    return CONF_OK;
}

char *
conf_set_hash(struct conf *cf, struct command *cmd, void *conf)
{
    uint8_t *p;
    hash_type_t *hp;
    struct string *value, *hash;

    p = conf;
    hp = (hash_type_t *)(p + cmd->offset);

    if (*hp != CONF_UNSET_HASH) {
        return "is a duplicate";
    }

    value = array_top(&cf->arg);

    for (hash = hash_strings; hash->len != 0; hash++) {
        if (string_compare(value, hash) != 0) {
            continue;
        }

        *hp = hash - hash_strings;

        return CONF_OK;
    }

    return "is not a valid hash";
}

char *
conf_set_distribution(struct conf *cf, struct command *cmd, void *conf)
{
    uint8_t *p;
    dist_type_t *dp;
    struct string *value, *dist;

    p = conf;
    dp = (dist_type_t *)(p + cmd->offset);

    if (*dp != CONF_UNSET_DIST) {
        return "is a duplicate";
    }

    value = array_top(&cf->arg);

    for (dist = dist_strings; dist->len != 0; dist++) {
        if (string_compare(value, dist) != 0) {
            continue;
        }

        *dp = dist - dist_strings;

        return CONF_OK;
    }

    return "is not a valid distribution";
}

char *
conf_set_hashtag(struct conf *cf, struct command *cmd, void *conf)
{
    rstatus_t status;
    uint8_t *p;
    struct string *field, *value;

    p = conf;
    field = (struct string *)(p + cmd->offset);

    if (field->data != CONF_UNSET_PTR) {
        return "is a duplicate";
    }

    value = array_top(&cf->arg);

    if (value->len != 2) {
        return "is not a valid hash tag string with two characters";
    }

    status = string_duplicate(field, value);
    if (status != NC_OK) {
        return CONF_ERROR;
    }

    return CONF_OK;
}

#if 1 //shenzheng 2014-9-11 replace server

struct yaml_event
{
	TAILQ_ENTRY(yaml_event_q)	yaml_event_tqe;
	yaml_event_t	event_t;
};

TAILQ_HEAD(yaml_event_tqh, yaml_event);

uint8_t string_prefix_of_fix_len_char(const struct string * s1, uint8_t * s2, size_t len)
{
	if (s1->len > len) {
        return 0;
    }
	uint32_t i;
	uint8_t prefix_flag;
	prefix_flag = 1;
	for(i = 0; i < s1->len; i ++)
	{
		if(*(s1->data + i) != *(s2 + i))
		{
			prefix_flag = 0;
			break;
		}
	}
	
    return prefix_flag;
}

rstatus_t
conf_write_back_yaml(struct context *ctx, struct string *old_ser, struct string *new_ser)
{
	rstatus_t status;
	FILE *input, *output;        
	yaml_parser_t parser;        
	struct yaml_event *event;       
	yaml_emitter_t emitter;
	int done = 0;
	bool onetime_flag = false;
	struct yaml_event_tqh event_queue;
	TAILQ_INIT(&event_queue);
	
	input = fopen(ctx->cf->fname, "rb");
	if(input == NULL)
	{
		log_error("error: conf file did't exit!");
		return NC_ERROR;
	}
	     
	yaml_parser_initialize(&parser);        
	yaml_parser_set_input_file(&parser, input);    

	while (!done)        
	{            
		event = nc_zalloc(sizeof(struct yaml_event));
		status = yaml_parser_parse(&parser, &event->event_t);
		if (!status) 
		{
			log_error("error: conf file is error(%d)", status);
			return NC_ERROR;
		}
		
		switch (event->event_t.type) {
			case YAML_NO_EVENT:
				log_debug(LOG_DEBUG,"YAML_NO_EVENT");
				break;
			case YAML_STREAM_START_EVENT:
				log_debug(LOG_DEBUG,"STREAM_START");
				break;
			case YAML_STREAM_END_EVENT:
				done = 1;
				log_debug(LOG_DEBUG,"STREAM_END");
				break;
			case YAML_DOCUMENT_START_EVENT:
				log_debug(LOG_DEBUG,"DOCUMENT_START");
				break;
			case YAML_DOCUMENT_END_EVENT:
				log_debug(LOG_DEBUG,"DOCUMENT_END");
				break;
			case YAML_ALIAS_EVENT:
				log_debug(LOG_DEBUG,"ALIAS");
				break;
    		case YAML_MAPPING_END_EVENT:
        		log_debug(LOG_DEBUG,"MAPPING_END");
        		break;
    		case YAML_MAPPING_START_EVENT:
        		log_debug(LOG_DEBUG,"MAPPING_START");
        		break;
    		case YAML_SEQUENCE_START_EVENT:
				log_debug(LOG_DEBUG,"SEQUENCE_START");
				break;
    		case YAML_SEQUENCE_END_EVENT:
        		log_debug(LOG_DEBUG,"SEQUENCE_END");
        		break;
    		case YAML_SCALAR_EVENT:
        		log_debug(LOG_DEBUG,"%.*s	"
					, event->event_t.data.scalar.length, event->event_t.data.scalar.value
					);
				break;
    		default:
        		NOT_REACHED();
        		break;
    	}

		TAILQ_INSERT_TAIL(&event_queue, event, yaml_event_tqe);
		
		//yaml_event_delete(&event->event_t);
		
	}        
	yaml_parser_delete(&parser);
	fclose(input);

	output = fopen(ctx->cf->fname, "wb");
	yaml_emitter_initialize(&emitter);
	yaml_emitter_set_output_file(&emitter, output);

	log_debug(LOG_DEBUG, "old_ser->len: %d", old_ser->len);
	log_debug(LOG_DEBUG, "old_ser->data: %s", old_ser->data);
	log_debug(LOG_DEBUG, "new_ser->len: %d", new_ser->len);
	log_debug(LOG_DEBUG, "new_ser->data: %s", new_ser->data);

	struct yaml_event *ev, *nev;
	void *ptr_tmp;
	for (ev = TAILQ_FIRST(&event_queue); ev != NULL; ev = nev) 
	{
        nev = TAILQ_NEXT(ev, yaml_event_tqe);

		if(onetime_flag == false && YAML_SCALAR_EVENT == ev->event_t.type && 
			1 == string_prefix_of_fix_len_char(old_ser
			, ev->event_t.data.scalar.value, ev->event_t.data.scalar.length))
		{			
			log_debug(LOG_DEBUG, "old_ser->data : %s", old_ser->data);
			uint32_t i, unchanging_len;
			yaml_char_t * old_data;
			old_data = ev->event_t.data.scalar.value;
			unchanging_len = ev->event_t.data.scalar.length - old_ser->len;
			
			ev->event_t.data.scalar.length = new_ser->len + unchanging_len;
			ev->event_t.data.scalar.value = nc_zalloc((new_ser->len + unchanging_len + 1) * sizeof(yaml_char_t));
			
			for(i = 0; i < new_ser->len; i ++)
			{
				*(ev->event_t.data.scalar.value + i) = *(new_ser->data + i);
			}
			for(i = 0; i < unchanging_len; i ++)
			{
				*(ev->event_t.data.scalar.value + new_ser->len + i) = *(old_data + old_ser->len + i);
			}
			nc_free(old_data);

			onetime_flag = true;
		}

		if (!yaml_emitter_emit(&emitter, &ev->event_t))
		{
			log_debug(LOG_DEBUG, "yaml_emitter_emit error");
		}

		//yaml_event_delete(&ev->event_t);
		//TAILQ_REMOVE(&event_qunue, ev, yaml_event_tqe);
		nc_free(ev);
	}

	/*
	while(!TAILQ_EMPTY(&event_queue))
	{
		ev = TAILQ_FIRST(&event_queue);
		
		TAILQ_REMOVE(&event_queue, ev, yaml_event_tqe);
		
		nc_free(ev);
	}
	*/
	yaml_emitter_delete(&emitter);
	fclose(output);

	return NC_OK;
}

#endif //shenzheng 2014-9-11 replace server

#if 1 //shenzheng 2015-1-8 log rotating
char *
conf_set_log_rorate(struct conf *cf, struct command *cmd, void *conf)
{
    uint8_t *p;
    struct string *value, true_str, false_str;

    p = conf;

    if (LOG_RORATE != CONF_UNSET_NUM) {
        return "is a duplicate";
    }

    value = array_top(&cf->arg);
    string_set_text(&true_str, "true");
    string_set_text(&false_str, "false");

    if (string_compare(value, &true_str) == 0) {
        LOG_RORATE = 1;
    } else if (string_compare(value, &false_str) == 0) {
        LOG_RORATE = 0;
    } else {
        return "is not \"true\" or \"false\"";
    }

    return CONF_OK;
}

char *
conf_set_log_file_max_size(struct conf *cf, struct command *cmd, void *conf)
{
	uint8_t *p;
	uint8_t ch;
    ssize_t num;
	uint8_t *pos;
    struct string *value;
	int i;
	ssize_t multiple_for_unit = 1;
	bool first_nonzero_flag = false;
    p = conf;
    if (LOG_FIEL_MAX_SIZE_FOR_ROTATING != CONF_UNSET_NUM) {
        return "is a duplicate";
    }
	
    value = array_top(&cf->arg);
	num = 0;
	if(value->len > 2)
	{
		pos = value->data + value->len - 2;
		if((*pos == 'G' || *pos == 'g') && (*(pos+1) == 'B' || *(pos+1) == 'b'))
		{
			multiple_for_unit = 1073741824;
			value->len -= 2;
		}
		else if((*pos == 'M' || *pos == 'm') && (*(pos+1) == 'B' || *(pos+1) == 'b'))
		{
			multiple_for_unit = 1048576;
			value->len -= 2;
		}
		else if(*(pos+1) == 'G' || *(pos+1) == 'g')
		{
    		multiple_for_unit = 1000000000;
			value->len -= 1;
		}
		else if(*(pos+1) == 'M' || *(pos+1) == 'm')
		{
			multiple_for_unit = 1000000;
			value->len -= 1;
		}
		else if(*(pos+1) == 'B' || *(pos+1) == 'b')
		{
			value->len -= 1;
		}
	}
	else if(value->len > 1)
	{
		pos = value->data + value->len - 1;
		if(*pos == 'G' || *pos == 'g')
		{   
			multiple_for_unit = 1000000000;
			value->len -= 1;
		}
		else if(*pos == 'M' || *pos == 'm')
		{   
			multiple_for_unit = 1000000;
			value->len -= 1;
		}
		else if(*pos == 'B' || *pos == 'b')
		{
		    value->len -= 1;
		}
	}
	else if(value->len <= 0)
	{
		return "is null";
	}

	for(i = 0; i < value->len; i ++)
	{
		ch = *(value->data + i);
		if(ch < '0' || ch > '9')
		{
			return "is not a number";
		}
		else if(!first_nonzero_flag && ch != '0')
		{
			first_nonzero_flag = true;
		}
		
		if(first_nonzero_flag)
		{
			num = 10*num + (ch - 48);
		}
	}
	num *= multiple_for_unit;

	if(first_nonzero_flag == false)
	{
		return "can not be 0";
	}

	LOG_FIEL_MAX_SIZE_FOR_ROTATING = num;
    return CONF_OK;
}

char *
conf_set_log_file_count(struct conf *cf, struct command *cmd, void *conf)
{
    uint8_t *p;
    int num;
    struct string *value;

    p = conf;
    if (LOG_FILE_COUNT_TO_STAY != CONF_UNSET_LOG_FILE_COUNT) {
        return "is a duplicate";
    }

    value = array_top(&cf->arg);

    num = nc_atoi(value->data, value->len);
    if (num < 0) {
        return "is not a number";
    }

	LOG_FILE_COUNT_TO_STAY = num;
    return CONF_OK;
}

#endif //shenzheng 2015-1-8 log rotating

#if 1 //shenzheng 2015-4-29 common
struct string *
hash_type_to_string(hash_type_t hash_type)
{
	struct string *hash_string = hash_strings + hash_type;

	return hash_string;
}
struct string *
dist_type_to_string(dist_type_t dist_type)
{
	struct string *dist_string = dist_strings + dist_type;

	return dist_string;
}

#endif //shenzheng 2015-4-29 common

#if 1 //shenzheng 2015-5-29 config-reload
static rstatus_t
conf_pool_each_compare(void *elem, void *data)
{
	bool flag = false;
	uint32_t i, j, nelem, server_count1, server_count2;
	struct conf_pool *cp1, *cp2;
	struct conf_server *cs1, *cs2;
	struct array *cps = data;

	cp1 = elem;
	for (i = 0, nelem = array_n(cps); i < nelem; i++) 
	{
		cp2 = array_get(cps, i);
		if(conf_pool_name_cmp(cp1, cp2) == 0)
		{
			flag = true;
			break;
		}
	}
	
	if(flag == false)
	{
		return NC_ERROR;
	}

	//listen
	if(conf_pool_listen_cmp(cp1, cp2) != 0)
	{
		return NC_ERROR;
	}

	//hash
	if(cp1->hash != cp2->hash)
	{
		return NC_ERROR;
	}

	//hash_tag
	if(string_compare(&cp1->hash_tag, &cp2->hash_tag) != 0)
	{
		return NC_ERROR;
	}

	//distribution
	if(cp1->distribution != cp2->distribution)
	{
		return NC_ERROR;
	}

	//timeout
	if(cp1->timeout != cp2->timeout)
	{
		return NC_ERROR;
	}

	//backlog
	if(cp1->backlog != cp2->backlog)
	{
		return NC_ERROR;
	}

	//preconnect
	if(cp1->preconnect != cp2->preconnect)
	{
		return NC_ERROR;
	}

	//redis
	if(cp1->redis != cp2->redis)
	{
		return NC_ERROR;
	}

    //redis_auth
    if(string_compare(&cp1->redis_auth, &cp2->redis_auth))
	{
		return NC_ERROR;
	}

	//server_connections
	if(cp1->server_connections != cp2->server_connections)
	{
		return NC_ERROR;
	}

	//auto_eject_hosts
	if(cp1->auto_eject_hosts != cp2->auto_eject_hosts)
	{
		return NC_ERROR;
	}

	//server_retry_timeout
	if(cp1->server_retry_timeout != cp2->server_retry_timeout)
	{
		return NC_ERROR;
	}

	//server_failure_limit
	if(cp1->server_failure_limit != cp2->server_failure_limit)
	{
		return NC_ERROR;
	}

	//servers
	server_count1 = array_n(&cp1->server);
	server_count2 = array_n(&cp2->server);
	if(server_count1 != server_count2)
	{
		return NC_ERROR;
	}
	flag = false;
	for (i = 0; i < server_count1; i++) 
	{
		cs1 = array_get(&cp1->server, i);
		for (j = 0; j < server_count2; j++) 
		{
			cs2 = array_get(&cp2->server, j);
			if(conf_server_name_cmp(cs1, cs2) == 0)
			{
				flag = true;
				break;
			}
		}
		if(flag == false)
		{
			return NC_ERROR;
		}
		if(string_compare(&cs1->pname, &cs2->pname) != 0)
		{
			return NC_ERROR;
		}
		flag = false;
	}

#if 1 //shenzheng 2015-6-16 tcpkeepalive
	//tcpkeepalive
	if(cp1->tcpkeepalive != cp2->tcpkeepalive)
	{
		return NC_ERROR;
	}

	//tcpkeepidle
	if(cp1->tcpkeepidle != cp2->tcpkeepidle)
	{
		return NC_ERROR;
	}

	//tcpkeepcnt
	if(cp1->tcpkeepcnt != cp2->tcpkeepcnt)
	{
		return NC_ERROR;
	}

	//tcpkeepintvl
	if(cp1->tcpkeepintvl != cp2->tcpkeepintvl)
	{
		return NC_ERROR;
	}
#endif //shenzheng 2015-6-16 tcpkeepalive

	return NC_OK;
}


rstatus_t
conf_two_check_diff(struct conf *cf, struct conf *cf_new)
{
	if(cf == NULL || cf_new == NULL)
	{
		return NC_ERROR;
	}

	if(array_n(&cf->pool) != array_n(&cf_new->pool))
	{
		return NC_OK;
	}
	
	if(NC_OK != array_each(&cf->pool, conf_pool_each_compare, &cf_new->pool))
	{
		return NC_OK;
	}

	return NC_ERROR;
}


static rstatus_t
conf_yaml_init_from_string(struct conf *cf, struct string *cf_s)
{
    int rv;

    ASSERT(!cf->valid_parser);

	if(cf_s == NULL)
	{
		log_error("conf: failed that conf string is null.");
		return NC_ERROR;
	}

	if(cf_s->data == NULL || cf_s->len == 0)
	{
		log_error("conf: failed that conf string content is null.");
		return NC_ERROR;
	}

	/*
    rv = fseek(cf->fh, 0L, SEEK_SET);
    if (rv < 0) {
        log_error("conf: failed to seek to the beginning of file '%s': %s",
                  cf->fname, strerror(errno));
        return NC_ERROR;
    }
	*/
    rv = yaml_parser_initialize(&cf->parser);
    if (!rv) {
        log_error("conf: failed (err %d) to initialize yaml parser",
                  cf->parser.error);
        return NC_ERROR;
    }

    //yaml_parser_set_input_file(&cf->parser, cf->fh);

	yaml_parser_set_input_string(&cf->parser, cf_s->data, cf_s->len);
    cf->valid_parser = 1;

    return NC_OK;
}


static rstatus_t
conf_validate_document_from_string(struct conf *cf, struct string *cf_s)
{
    rstatus_t status;
    uint32_t count;
    bool done;

    status = conf_yaml_init_from_string(cf, cf_s);
    if (status != NC_OK) {
        return status;
    }

    count = 0;
    done = false;
    do {
        yaml_document_t document;
        yaml_node_t *node;
        int rv;

        rv = yaml_parser_load(&cf->parser, &document);
        if (!rv) {
            log_error("conf: failed (err %d) to get the next yaml document",
                      cf->parser.error);
            conf_yaml_deinit(cf);
            return NC_ERROR;
        }

        node = yaml_document_get_root_node(&document);
        if (node == NULL) {
            done = true;
        } else {
            count++;
        }

        yaml_document_delete(&document);
    } while (!done);

    conf_yaml_deinit(cf);

    if (count != 1) {
        log_error("conf: '%s' must contain only 1 document; found %"PRIu32" "
                  "documents", cf->fname, count);
        return NC_ERROR;
    }

    return NC_OK;
}

static rstatus_t
conf_validate_tokens_from_string(struct conf *cf, struct string *cf_s)
{
    rstatus_t status;
    bool done, error;
    int type;

    status = conf_yaml_init_from_string(cf, cf_s);
    if (status != NC_OK) {
        return status;
    }

    done = false;
    error = false;
    do {
        status = conf_token_next(cf);
        if (status != NC_OK) {
            return status;
        }
        type = cf->token.type;

        switch (type) {
        case YAML_NO_TOKEN:
            error = true;
            log_error("conf: no token (%d) is disallowed", type);
            break;

        case YAML_VERSION_DIRECTIVE_TOKEN:
            error = true;
            log_error("conf: version directive token (%d) is disallowed", type);
            break;

        case YAML_TAG_DIRECTIVE_TOKEN:
            error = true;
            log_error("conf: tag directive token (%d) is disallowed", type);
            break;

        case YAML_DOCUMENT_START_TOKEN:
            error = true;
            log_error("conf: document start token (%d) is disallowed", type);
            break;

        case YAML_DOCUMENT_END_TOKEN:
            error = true;
            log_error("conf: document end token (%d) is disallowed", type);
            break;

        case YAML_FLOW_SEQUENCE_START_TOKEN:
            error = true;
            log_error("conf: flow sequence start token (%d) is disallowed", type);
            break;

        case YAML_FLOW_SEQUENCE_END_TOKEN:
            error = true;
            log_error("conf: flow sequence end token (%d) is disallowed", type);
            break;

        case YAML_FLOW_MAPPING_START_TOKEN:
            error = true;
            log_error("conf: flow mapping start token (%d) is disallowed", type);
            break;

        case YAML_FLOW_MAPPING_END_TOKEN:
            error = true;
            log_error("conf: flow mapping end token (%d) is disallowed", type);
            break;

        case YAML_FLOW_ENTRY_TOKEN:
            error = true;
            log_error("conf: flow entry token (%d) is disallowed", type);
            break;

        case YAML_ALIAS_TOKEN:
            error = true;
            log_error("conf: alias token (%d) is disallowed", type);
            break;

        case YAML_ANCHOR_TOKEN:
            error = true;
            log_error("conf: anchor token (%d) is disallowed", type);
            break;

        case YAML_TAG_TOKEN:
            error = true;
            log_error("conf: tag token (%d) is disallowed", type);
            break;

        case YAML_BLOCK_SEQUENCE_START_TOKEN:
        case YAML_BLOCK_MAPPING_START_TOKEN:
        case YAML_BLOCK_END_TOKEN:
        case YAML_BLOCK_ENTRY_TOKEN:
            break;

        case YAML_KEY_TOKEN:
        case YAML_VALUE_TOKEN:
        case YAML_SCALAR_TOKEN:
            break;

        case YAML_STREAM_START_TOKEN:
            break;

        case YAML_STREAM_END_TOKEN:
            done = true;
            log_debug(LOG_VVERB, "conf '%s' has valid tokens", cf->fname);
            break;

        default:
            error = true;
            log_error("conf: unknown token (%d) is disallowed", type);
            break;
        }

        conf_token_done(cf);
    } while (!done && !error);

    conf_yaml_deinit(cf);

    return !error ? NC_OK : NC_ERROR;
}

static rstatus_t
conf_validate_structure_from_string(struct conf *cf, struct string *cf_s)
{
    rstatus_t status;
    int type, depth;
    uint32_t i, count[CONF_MAX_DEPTH + 1];
    bool done, error, seq;

    status = conf_yaml_init_from_string(cf, cf_s);
    if (status != NC_OK) {
        return status;
    }

    done = false;
    error = false;
    seq = false;
    depth = 0;
    for (i = 0; i < CONF_MAX_DEPTH + 1; i++) {
        count[i] = 0;
    }

    /*
     * Validate that the configuration conforms roughly to the following
     * yaml tree structure:
     *
     * keyx:
     *   key1: value1
     *   key2: value2
     *   seq:
     *     - elem1
     *     - elem2
     *     - elem3
     *   key3: value3
     *
     * keyy:
     *   key1: value1
     *   key2: value2
     *   seq:
     *     - elem1
     *     - elem2
     *     - elem3
     *   key3: value3
     */
    do {
        status = conf_event_next(cf);
        if (status != NC_OK) {
            return status;
        }

        type = cf->event.type;

        log_debug(LOG_VVERB, "next event %d depth %d seq %d", type, depth, seq);

        switch (type) {
        case YAML_STREAM_START_EVENT:
        case YAML_DOCUMENT_START_EVENT:
            break;

        case YAML_DOCUMENT_END_EVENT:
            break;

        case YAML_STREAM_END_EVENT:
            done = true;
            break;

        case YAML_MAPPING_START_EVENT:
            if (depth == CONF_ROOT_DEPTH && count[depth] != 1) {
                error = true;
                log_error("conf: '%s' has more than one \"key:value\" at depth"
                          " %d", cf->fname, depth);
            } else if (depth >= CONF_MAX_DEPTH) {
                error = true;
                log_error("conf: '%s' has a depth greater than %d", cf->fname,
                          CONF_MAX_DEPTH);
            }
            depth++;
            break;

        case YAML_MAPPING_END_EVENT:
            if (depth == CONF_MAX_DEPTH) {
                if (seq) {
                    seq = false;
                } else {
                    error = true;
                    log_error("conf: '%s' missing sequence directive at depth "
                              "%d", cf->fname, depth);
                }
            }
            depth--;
            count[depth] = 0;
            break;

        case YAML_SEQUENCE_START_EVENT:
            if (seq) {
                error = true;
                log_error("conf: '%s' has more than one sequence directive",
                          cf->fname);
            } else if (depth != CONF_MAX_DEPTH) {
                error = true;
                log_error("conf: '%s' has sequence at depth %d instead of %d",
                          cf->fname, depth, CONF_MAX_DEPTH);
            } else if (count[depth] != 1) {
                error = true;
                log_error("conf: '%s' has invalid \"key:value\" at depth %d",
                          cf->fname, depth);
            }
            seq = true;
            break;

        case YAML_SEQUENCE_END_EVENT:
            ASSERT(depth == CONF_MAX_DEPTH);
            count[depth] = 0;
            break;

        case YAML_SCALAR_EVENT:
            if (depth == 0) {
                error = true;
                log_error("conf: '%s' has invalid empty \"key:\" at depth %d",
                          cf->fname, depth);
            } else if (depth == CONF_ROOT_DEPTH && count[depth] != 0) {
                error = true;
                log_error("conf: '%s' has invalid mapping \"key:\" at depth %d",
                          cf->fname, depth);
            } else if (depth == CONF_MAX_DEPTH && count[depth] == 2) {
                /* found a "key: value", resetting! */
                count[depth] = 0;
            }
            count[depth]++;
            break;

        default:
            NOT_REACHED();
        }

        conf_event_done(cf);
    } while (!done && !error);

    conf_yaml_deinit(cf);

    return !error ? NC_OK : NC_ERROR;
}

static rstatus_t
conf_pre_validate_from_string(struct conf *cf, struct string *cf_s)
{
    rstatus_t status;

    status = conf_validate_document_from_string(cf, cf_s);
    if (status != NC_OK) {
        return status;
    }

    status = conf_validate_tokens_from_string(cf, cf_s);
    if (status != NC_OK) {
        return status;
    }

    status = conf_validate_structure_from_string(cf, cf_s);
    if (status != NC_OK) {
        return status;
    }

    cf->sound = 1;

    return NC_OK;
}

static rstatus_t
conf_begin_parse_from_string(struct conf *cf, struct string *cf_s)
{
    rstatus_t status;
    bool done;

    ASSERT(cf->sound && !cf->parsed);
    ASSERT(cf->depth == 0);

    status = conf_yaml_init_from_string(cf, cf_s);
    if (status != NC_OK) {
        return status;
    }

    done = false;
    do {
        status = conf_event_next(cf);
        if (status != NC_OK) {
            return status;
        }

        log_debug(LOG_VVERB, "next begin event %d", cf->event.type);

        switch (cf->event.type) {
        case YAML_STREAM_START_EVENT:
        case YAML_DOCUMENT_START_EVENT:
            break;

        case YAML_MAPPING_START_EVENT:
            ASSERT(cf->depth < CONF_MAX_DEPTH);
            cf->depth++;
            done = true;
            break;

        default:
            NOT_REACHED();
        }

        conf_event_done(cf);

    } while (!done);

    return NC_OK;
}

static rstatus_t
conf_parse_from_string(struct conf *cf, struct string *cf_s)
{
    rstatus_t status;

    ASSERT(cf->sound && !cf->parsed);
    ASSERT(array_n(&cf->arg) == 0);

    status = conf_begin_parse_from_string(cf, cf_s);
    if (status != NC_OK) {
        return status;
    }

    status = conf_parse_core(cf, NULL);
    if (status != NC_OK) {
        return status;
    }

    status = conf_end_parse(cf);
    if (status != NC_OK) {
        return status;
    }

    cf->parsed = 1;

    return NC_OK;
}

struct conf *
conf_create_from_string(struct string *cf_s)
{
    rstatus_t status;
    struct conf *cf;

    //cf = conf_open(filename);
    //if (cf == NULL) {
    //    return NULL;
    //}
	
	cf = nc_alloc(sizeof(*cf));
	if (cf == NULL) {
	   return NULL;
	}
	
	status = array_init(&cf->arg, CONF_DEFAULT_ARGS, sizeof(struct string));
	if (status != NC_OK) {
	   nc_free(cf);
	   return NULL;
	}

	status = array_init(&cf->pool, CONF_DEFAULT_POOL, sizeof(struct conf_pool));
	if (status != NC_OK) {
	   array_deinit(&cf->arg);
	   nc_free(cf);
	   return NULL;
	}
	
	//cf->fname = filename;
	cf->fname = NULL;
	cf->fh = NULL;
	cf->depth = 0;
	/* parser, event, and token are initialized later */
	cf->seq = 0;
	cf->valid_parser = 0;
	cf->valid_event = 0;
	cf->valid_token = 0;
	cf->sound = 0;
	cf->parsed = 0;
	cf->valid = 0;

    /* validate configuration file before parsing */
    status = conf_pre_validate_from_string(cf, cf_s);
    if (status != NC_OK) {
        goto error;
    }

    /* parse the configuration file */
    status = conf_parse_from_string(cf, cf_s);
    if (status != NC_OK) {
        goto error;
    }

    /* validate parsed configuration */
    status = conf_post_validate(cf);
    if (status != NC_OK) {
        goto error;
    }

    conf_dump(cf);

    //fclose(cf->fh);
    //cf->fh = NULL;
	ASSERT(cf->fh == NULL);
	
    return cf;

error:
    //fclose(cf->fh);
    //cf->fh = NULL;
    ASSERT(cf->fh == NULL);
    conf_destroy(cf);
    return NULL;
}

/**This function is multithreading. 
  **So we use ctx->reload_thread and stats->reload_thread
  **to control the timing between multiple threads.
  **argument msg just for proxy admin, other thread can not use this arg.
  **/
rstatus_t
conf_reload(struct context *ctx, struct conn *conn, 
		conf_parse_type_t parse_type, struct string *cf_s,
		struct msg * msg)
{
	rstatus_t status;
	struct conf *cf, *cf_swap = NULL;
	struct array *pools_curr, *pools_new;
	struct array server_pools;
	struct stats *stats;
	char *contents = NULL;
	uint32_t npool;
	int sd_reload;
	struct conn *conn_reload;

	if(conn != NULL)
	{
		ASSERT(conn->client && !conn->proxy);
   		ASSERT(conn->owner == ctx);
	}

	array_null(&server_pools);

	pthread_mutex_lock(&ctx->reload_lock);
	
	stats = ctx->stats;

	if(ctx->reload_thread || stats->reload_thread)
	{
		contents = "reload conf failed, please retry.";
		status = NC_EAGAIN;
		goto done;
	}

	if(parse_type == CONF_PARSE_FILE)
	{
		cf_swap = conf_create(ctx->cf->fname);
	}
	else if(parse_type == CONF_PARSE_STRING)
	{
		cf_swap = conf_create_from_string(cf_s);
	}
	else
	{
		contents = "reload conf failed, parse type error.";
	}
	
	if(cf_swap == NULL)
	{
		status = NC_ERROR;
		if(contents == NULL)
		{
			contents = "reload conf failed, conf create error.";
		}
		goto done;
	}

	//status = NC_OK;
	status = conf_two_check_diff(ctx->cf, cf_swap);
	if(status != NC_OK)
	{
		contents = "reload conf failed, new conf is same as current.";
		status = NC_ERROR;
		goto done;
	}
	
	if(ctx->which_pool)
	{
		pools_curr = &ctx->pool_swap;
		pools_new = &ctx->pool;
	}
	else
	{
		pools_curr = &ctx->pool;
		pools_new = &ctx->pool_swap;
	}
	
	npool = array_n(&cf_swap->pool);
	ASSERT(npool > 0);
	
	status = server_pool_init(&server_pools, &cf_swap->pool, ctx);
    if (status != NC_OK) {
    	contents = "reload conf failed, init server pool error.";
		status = NC_ERROR;
		goto done;
    }
	
	stats->pause = 1;
	
	status = array_each(&server_pools, server_pool_each_proxy_conn_new, pools_curr);
	if(status != NC_OK)
	{
		stats->pause = 0;
		contents = "reload conf failed, new pool's ip or port error.";
		status = NC_ERROR;
		goto done;
	}

	cf = cf_swap;
	cf_swap = ctx->cf_swap;
	ctx->cf_swap = cf;

	ASSERT(ctx->reload_thread == 0);	
	array_swap(&server_pools, pools_new);
	ctx->reload_thread = 1;

	/* to notice the main thread */
	sd_reload = socket(AF_INET, SOCK_STREAM, 0);
    if (sd_reload < 0) {
        contents = "reload conf failed, socket get failed.";
        return NC_ERROR;
		goto done;
    }
	conn_reload = conn_get_for_reload(ctx);
	conn_reload->sd = sd_reload;
	status = event_add_conn(ctx->evb, conn_reload);
	if(status < 0)
	{
		stats->pause = 0;
		contents = "reload conf failed, event add error.";
		status = NC_ERROR;
		goto done;
	}

	contents = "conf reload success.";
	status = NC_OK;
done:
	pthread_mutex_unlock(&ctx->reload_lock);
	if(cf_swap != NULL)
	{
		conf_destroy(cf_swap);
	}
	if(server_pools.elem != NULL)
	{
		if(array_n(&server_pools) > 0)
		{
			array_each(&server_pools, proxy_each_deinit_for_reload, NULL);
		}
		server_pool_deinit(&server_pools);
	}
	
	if(msg != NULL)
	{
		rstatus_t status_tmp;
		status_tmp = msg_append_proxy_adm(msg, (uint8_t *)contents, strlen(contents));
	    if (status_tmp != NC_OK) {
			if(conn != NULL)
			{
				conn->err = ENOMEM;
			}
	        return status;
	    }

		status_tmp = msg_append_proxy_adm(msg, (uint8_t *)CRLF, CRLF_LEN);
		if (status_tmp != NC_OK) {
			if(conn != NULL)
			{
				conn->err = ENOMEM;
			}
			return status;
		}
	}
	else
	{
		log_error("%s", contents);
	}
	
	return status;
}

#endif //shenzheng 2015-5-30 config-reload

#if 1 //shenzheng 2015-6-6 zookeeper
#ifdef NC_ZOOKEEPER

struct conf *
conf_create_from_zk(struct context * ctx, char *zk_servers, char *zk_path)
{
	rstatus_t status;
	void* zkhandle = NULL;
	struct conf *cf;
	struct string cf_s;
	
	cf_s.data = nc_zalloc(Zk_MAX_DATA_LEN*sizeof(cf_s.data));
	cf_s.len = Zk_MAX_DATA_LEN;
	if(cf_s.data == NULL)
	{
		return NULL;
	}

	zkhandle = zk_init(zk_servers);
	
	if(zkhandle == NULL)
	{
		log_error("get zookeeper handle error.");
		return NULL;
	}
	ctx->zkhandle = zkhandle;
	
	status = zk_get(zkhandle, zk_path, &cf_s);
	if(status != NC_OK)
	{
		log_error("zk_get error(%d).", status);
		string_deinit(&cf_s);
		return NULL;
	}

	log_debug(LOG_DEBUG, "zookeeper data(%d) : %s", cf_s.len, cf_s.data);

	cf = conf_create_from_string(&cf_s);

	if(cf != NULL)
	{
		if(!string_empty(&ctx->watch_path))
		{
			string_deinit(&ctx->watch_path);
		}
		string_copy(&ctx->watch_path, (uint8_t *)zk_path, strlen(zk_path));

		if(!string_empty(&ctx->zk_servers))
		{
			string_deinit(&ctx->zk_servers);
		}
		string_copy(&ctx->zk_servers, (uint8_t *)zk_servers, strlen(zk_servers));
	}

	string_deinit(&cf_s);

	return cf;
	
}

rstatus_t 
conf_keep_from_zk(struct context * ctx, void *zkhandle, char *zk_path)
{
	rstatus_t status;

	status = zk_conf_set_watcher(zkhandle, zk_path, ctx);

	if(status != NC_OK)
	{
		log_error("conf keep from zookeeper error(%d)", status);
		return status;
	}

	if(!string_empty(&ctx->watch_path))
	{
		string_deinit(&ctx->watch_path);
	}
	string_copy(&ctx->watch_path, (uint8_t *)zk_path, strlen(zk_path));

	return NC_OK;
}

#endif
#endif //shenzheng 2015-6-9 zookeeper


