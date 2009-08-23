/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2006, 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: sa_state.c 21807 2008-12-11 22:00:31Z cnelluri $
 */

#include "sa_kernel.h"
#include "sa_assert.h"
#include "sa_log.h"
#include "sa_state.h"


/*
 * Transition descriptor table size limit of 256 will allow the transition
 * table to contain just bytes.  A larger limit will make it twice as big.
 * Don't make this more than 65536.
 */
#ifdef SA_STATE_DESC_LARGE		/* not usually defined */
#define SA_STATE_DESC_LIMIT (1 << 16)	/* max fc_sess_desc table size */
typedef u_int16_t sa_state_t;
#else
#define SA_STATE_DESC_LIMIT 256		/* max fc_sess_desc table size */
typedef u_int8_t sa_state_t;
#endif /* SA_STATE_DESC_LARGE */

/*
 * State descriptor structure.
 */
struct sa_state {
	const char	*st_name;	/* state name */
	sa_state_handler_t *st_handler;	/* handler function if any */
};

/*
 * State transition table handling.
 */
struct sa_state_table {
	const char	*st_name;	/* name of state table */
	const struct sa_state_desc *st_desc;	/* condensed description */
	uint		st_desc_limit;	/* size of condensed desc. table */
	void		(*st_log_func)(void *, const char *);	/* log func */
	u_short		st_state_limit;	/* limit on state number */
	u_short		st_event_limit;	/* limit on event index */
	struct sa_state	*st_states;	/* array of state structures */
	const char	**st_event_name;

	/*
	 * Variable sized state table, indexed by state and event.
	 */
	sa_state_t	st_next_table[0];

	/*
	 * Followed by array of struct sa_state
	 * and then by event names pointers.
	 */
};

/*
 * Return the transition table entry for state, event
 */
static inline sa_state_t *sa_state_table_entry(struct sa_state_table *tp,
					       u_int state, u_int event)
{
	sa_state_t *ep;

	ASSERT(state < tp->st_state_limit);
	ASSERT(event < tp->st_event_limit);

	ep = &tp->st_next_table[state * tp->st_event_limit + event];
	ASSERT(*ep < tp->st_desc_limit);
	return ep;
}

/*
 * State transition table initialization.
 */
struct sa_state_table *sa_state_table_alloc(
					    const char *name,
					    const struct sa_state_desc
					    *state_desc)
{
	u_int state_limit = 0;
	u_int event_limit = 0;
	u_int cur_state = 0;
	u_int desc;
	int error = 0;
	size_t len;
	size_t table_size;
	u_int event;
	u_int state;
	const struct sa_state_desc *dp;
	struct sa_state_table *tp;
	struct sa_state *sp;
	sa_state_t *ep;

	/*
	 * Find the dimensions of the table we need.
	 * Validate the table while we're at it.
	 */
	for (dp = state_desc; dp->sd_type != SST_END; dp++) {
		switch (dp->sd_type) {
		case SST_STATE:
			if (dp->sd_in == 0) {
				SA_LOG("state table %s has invalid state %d "
					"at STATE entry %d",
					name, dp->sd_in, dp - state_desc);
				error++;
			}
			if (dp->sd_in >= state_limit)
				state_limit = dp->sd_in + 1;
			cur_state = dp->sd_in;
			break;
		case SST_FROM:
			if (dp->sd_in == 0) {
				SA_LOG("state table %s has invalid state %d "
					"at FROM entry %d",
					name, dp->sd_in, dp - state_desc);
				error++;
			}
			cur_state = dp->sd_in;
			break;
		case SST_EVENT:
			if (dp->sd_in == 0) {
				SA_LOG("state table %s has invalid event %d "
					"at EVENT entry %d",
					name, dp->sd_in, dp - state_desc);
				error++;
			}
			if (dp->sd_in >= event_limit)
				event_limit = dp->sd_in + 1;
			break;
		case SST_HANDLER:
			if (dp->sd_u.sd_handler == NULL) {
				SA_LOG("state table %s has invalid hander %d "
					"at HANDLER entry %d",
					name, dp->sd_in, dp - state_desc);
				error++;
			}
			if (dp->sd_in >= state_limit)
				state_limit = dp->sd_in + 1;
			break;
		case SST_NEXT:
			if (cur_state == 0) {
				SA_LOG("state table %s has no current state "
					"for NEXT entry %d",
					name, dp - state_desc);
				error++;
			}
			if (dp->sd_in >= event_limit) {
				SA_LOG("state table %s has event %d "
					"out of range at NEXT entry %d",
					name, dp->sd_in, dp - state_desc);
				error++;
			}
			if (dp->sd_next == 0 || dp->sd_next >= state_limit) {
				SA_LOG("state table %s has state %d "
					"out of range at NEXT entry %d",
					name, dp->sd_next, dp - state_desc);
				error++;
			}
			break;
		case SST_END:
			break;
		}
	}

	/*
	 * Allocate the state transition table.
	 */
	if (error) {
		tp = NULL;
		goto out;
	}
	ASSERT_NOTIMPL(state_limit * event_limit <= SA_STATE_DESC_LIMIT);
	table_size = state_limit * event_limit * sizeof(sa_state_t);
	table_size = (table_size + sizeof(char *) - 1) & ~(sizeof(char *) - 1);
	len = sizeof(*tp) + table_size +
	    state_limit * sizeof(struct sa_state) +
	    event_limit * sizeof(char *);
	tp = sa_malloc(len);
	if (!tp)
		goto out;
	memset(tp, 0, len);
	tp->st_name = name;
	tp->st_desc = state_desc;
	tp->st_desc_limit = (uint) (dp - state_desc);
	ASSERT_NOTIMPL(tp->st_desc_limit <= SA_STATE_DESC_LIMIT);
	tp->st_state_limit = (u_short)state_limit;
	tp->st_event_limit = (u_short)event_limit;
	ep = (sa_state_t *) (tp + 1);
	tp->st_event_name = (const char **) (ep + table_size / sizeof(*ep));
	tp->st_states = (struct sa_state *) (tp->st_event_name + event_limit);
	ASSERT(len == (size_t)((char *) (tp->st_states + state_limit) - 
		(char *) tp));
	cur_state = 0;

	/*
	 * Set up the big table from the compact descriptor table.
	 */
	for (dp = state_desc, desc = 0; dp->sd_type != SST_END; dp++, desc++) {
		switch (dp->sd_type) {
		case SST_STATE:
			cur_state = dp->sd_in;
			ASSERT(cur_state < state_limit);
			sp = &tp->st_states[cur_state];
			sp->st_name = dp->sd_u.sd_name;
			break;
		case SST_EVENT:
			ASSERT(dp->sd_in < event_limit);
			tp->st_event_name[dp->sd_in] = dp->sd_u.sd_name;
			break;
		case SST_HANDLER:
			cur_state = dp->sd_in;
			ASSERT(cur_state < state_limit);
			sp = &tp->st_states[cur_state];
			sp->st_handler = dp->sd_u.sd_handler;
			break;
		case SST_FROM:
			ASSERT(dp->sd_in < state_limit);
			cur_state = dp->sd_in;
			break;
		case SST_NEXT:
			ASSERT(dp->sd_next < state_limit);
			ASSERT(desc <= SA_STATE_DESC_LIMIT);
			ep = sa_state_table_entry(tp, cur_state, dp->sd_in);
			*ep = (sa_state_t)desc;
			break;
		case SST_END:
			break;
		}
	}

	/*
	 * Go through the names entries and make sure none are NULL.
	 */
	tp->st_event_name[0] = "none";
	for (event = 0; event < event_limit; event++)
		ASSERT(tp->st_event_name[event] != NULL);
	tp->st_states[0].st_name = "none";
	for (state = 0; state < state_limit; state++)
		ASSERT(tp->st_states[state].st_name != NULL);
out:
	return tp;
}

/*
 * Free state table.
 */
void sa_state_table_free(struct sa_state_table *tp)
{
	sa_free(tp);
}

/*
 * Get an event name.
 */
const char *sa_state_event_name(struct sa_state_table *tp, u_int event)
{
	ASSERT(event < tp->st_event_limit);
	return tp->st_event_name[event];
}

/*
 * Get a state name.
 */
const char *sa_state_name(struct sa_state_table *tp, u_int state)
{
	ASSERT(state < tp->st_state_limit);
	return tp->st_states[state].st_name;
}


/*
 * Run a step of the state transition table.
 */
void
sa_state_table_step(struct sa_state_table *tp, void *statep_arg,
			u_int event, void *arg)
{
	u_int *statep = statep_arg;
	u_int old_state;
	u_int next;
	sa_state_t entry;
	const struct sa_state_desc *np;
	sa_state_handler_t *handler;

	old_state = *statep;
	ASSERT(old_state < tp->st_state_limit);
	ASSERT(event < tp->st_event_limit);

	next = 0;
	handler = NULL;
	entry = *sa_state_table_entry(tp, old_state, event);
	if (entry) {
		np = &tp->st_desc[entry];
		ASSERT(np->sd_type == SST_NEXT);
		next = np->sd_next;
		ASSERT(next < tp->st_state_limit);
		handler = np->sd_u.sd_handler;
	}
	if (handler) {
		(*handler)(arg);
	} else if (next != 0) {
		sa_state_table_enter(tp, statep, next, event, arg);
	} else {
		SA_LOG("state_table %s state %s (%d) "
			"event %s (%d) has no handler",
			tp->st_name, tp->st_states[old_state].st_name,
			old_state, tp->st_event_name[event], event);
		if (tp->st_log_func) {
			char buf[128];

			snprintf(buf, sizeof(buf),
				 "state %s (%d) event %s (%d) has no handler",
				 tp->st_states[old_state].st_name,
				 old_state, tp->st_event_name[event], event);
			(*tp->st_log_func)(arg, buf);
		}
	}
}

/*
 * Transition to the specified state.  Run the handler if any.
 */
void
sa_state_table_enter(struct sa_state_table *tp, void *statep_arg,
			u_int next, u_int event, void *arg)
{
	u_int *statep = statep_arg;
	u_int old_state;
	sa_state_handler_t *handler;
	struct sa_state *sp;

	old_state = *statep;
	ASSERT(old_state < tp->st_state_limit);
	ASSERT(next < tp->st_state_limit);

	sp = &tp->st_states[next];
	if (tp->st_log_func != NULL) {
		char buf[128];

		snprintf(buf, sizeof(buf), "event %s state %s -> %s",
			 tp->st_event_name[event],
			 tp->st_states[old_state].st_name, sp->st_name);
		(*tp->st_log_func)(arg, buf);
	}
	*statep = next;
	handler = sp->st_handler;
	if (handler)
		(*handler)(arg);
}

/*
 * sa_state_table_log() - set function to log state table transitions.
 */
void
sa_state_table_log(struct sa_state_table *tp,
		   void (*log_func)(void *arg, const char *msg))
{
	tp->st_log_func = log_func;
}
