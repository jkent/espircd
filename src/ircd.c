#include <esp8266.h>
#include "ircd.h"

#define DEBUG

static Ircd ircd;

static void ICACHE_FLASH_ATTR
ircSend(IrcUser *to, const char *msg)
{
	char buf[MSGLEN + 3];

	snprintf(buf, sizeof(buf), "%s\r\n", msg);
#ifdef DEBUG
	printf("u%2d << %s\n", to->index, msg);
#endif

	memcpy(ircd.conn.proto.tcp->remote_ip, to->remote_ip, 4);
	ircd.conn.proto.tcp->remote_port = to->remote_port;
	espconn_sent(&ircd.conn, (unsigned char *)buf, strlen(buf));
}

static void ICACHE_FLASH_ATTR
ircBroadcast(IrcUser *user, const char *msg)
{
	IrcUser *other;
	IrcChan *chan;
	int i, j;

	for (i = 0; i < MAX_USERS; i++) {
		other = &ircd.users[i];
		if (!(other->flags & USER_FLAG_CONNECTED)) {
			continue;
		}

		for (j = 0; j < MAX_CHANS; j++) {
			chan = &ircd.chans[j];
			if (!chan->users) {
				continue;
			}

			if (!(chan->user_flags[user->index] & CHAN_USER_FLAG_JOINED)) {
				continue;
			}

			if (!(chan->user_flags[i] & CHAN_USER_FLAG_JOINED)) {
				continue;
			}

			ircSend(other, msg);
			break;
		}
	}
}

static void ICACHE_FLASH_ATTR
ircDisconnect(IrcUser *user, const char *cmd, const char *reason_prefix,
			  const char *reason)
{
	IrcChan *chan;
	char buf[MSGLEN + 1];
	int i;

	snprintf(buf, sizeof(buf), ":%s!%s@" IPSTR " %s :%s%s", user->nick,
	         user->user, IP2STR(&user->remote_ip), cmd,
	         reason_prefix ? reason_prefix : "", reason ? reason : "");
	ircBroadcast(user, buf);

	for (i = 0; i < MAX_CHANS; i++) {
		chan = &ircd.chans[i];
		if (!chan->users) {
			continue;
		}

		if (!(chan->user_flags[user->index] & CHAN_USER_FLAG_JOINED)) {
			continue;
		}

		chan->user_flags[user->index] &= ~CHAN_USER_FLAG_JOINED;
		chan->users--;
	}

	if (user->flags & USER_FLAG_CONNECTED) {
		snprintf(buf, sizeof(buf), "ERROR :Closing Link: %s[" IPSTR "] "
		         "(%s%s)", user->nick, IP2STR(&user->remote_ip),
		         reason_prefix ? reason_prefix : "", reason ? reason : "");
		ircSend(user, buf);
		espconn_disconnect(&ircd.conn); // remote_ip/port set by ircSend
		user->flags &= ~USER_FLAG_CONNECTED;
#ifdef DEBUG
		printf("u%2d disconnected\n", user->index);
#endif
	}
}

static void ICACHE_FLASH_ATTR
ircUserFlagsToMode(char mode[], uint16 flags, uint16 mask)
{
	uint16 test;
	char *p = mode;
	int i, j;

	for (i = 0; i < 2; i++) {
		test = (i == 0) ? (flags & mask) : (~flags & mask);
		if (!test) {
			continue;
		}
		*p++ = (i == 0) ? '+' : '-';
		for (j = 0; j < 16; j++) {
			switch (test & (1 << j)) {
			case USER_FLAG_WALLOPS:   *p++ = 'w'; break;
			case USER_FLAG_INVISIBLE: *p++ = 'i'; break;
			case USER_FLAG_OPERATOR:  *p++ = 'o'; break;
			}
		}
	}
	*p = 0;
}

#if 0
static void ICACHE_FLASH_ATTR
ircChanFlagsToMode(char mode[], uint16 flags, uint16 mask)
{
	uint16 test;
	char *p = mode;
	int i, j;

	for (i = 0; i < 2; i++) {
		test = (i == 0) ? (flags & mask) : (~flags & mask);
		if (!test) {
			continue;
		}
		*p++ = (i == 0) ? '+' : '-';
		for (j = 0; j < 16; i++) {
			switch (test & (1 << j)) {
			case CHAN_FLAG_SECRET:	*p++ = 's'; break;
			case CHAN_FLAG_MODERATED: *p++ = 'm'; break;
			case CHAN_FLAG_NOOUTSIDE: *p++ = 'n'; break;
			case CHAN_FLAG_TOPICLOCK: *p++ = 't'; break;
			}
		}
	}
	*p = 0;
}
#endif

static bool ICACHE_FLASH_ATTR
ircGetUserChans(IrcUser *user, char *buf, size_t buflen)
{
	static IrcUser *last_user = NULL;
	static int chan_index = 0;
	IrcChan *chan;
	int len, count;

	if (last_user != user) {
		chan_index = 0;
		last_user = user;
	}

	if (chan_index >= MAX_CHANS) {
		chan_index = 0;
		return false;
	}

	count = 0;
	for (; chan_index < MAX_CHANS; chan_index++) {
		chan = &ircd.chans[chan_index];

		if (!chan->users) {
			continue;
		}

		if (!(chan->user_flags[user->index] & CHAN_USER_FLAG_JOINED)) {
			continue;
		}

		len = strlen(chan->name);
		if (len + 4 > buflen) {
			break;
		}

		if (count++ > 0) {
			*buf++ = ' ';
			buflen--;
		}

		if (chan->user_flags[user->index] & CHAN_USER_FLAG_CHANOP) {
			*buf++ = '@';
			buflen--;
		} else if (chan->user_flags[user->index] & CHAN_USER_FLAG_VOICE) {
			*buf++ = 'v';
			buflen--;
		}

		*buf++ = '#';
		buflen--;

		strcpy(buf, chan->name);
		buf += len;
		buflen -= len;
	}
	*buf = '\0';

	if (!count) {
		chan_index = 0;
	}

	return !!count;
}

static bool ICACHE_FLASH_ATTR
ircGetChanUsers(IrcChan *chan, char *buf, size_t buflen, bool invisible)
{
	static IrcChan *last_chan = NULL;
	static int user_index = 0;
	IrcUser *user;
	int len, count;

	if (last_chan != chan) {
		user_index = 0;
		last_chan = chan;
	}

	if (user_index >= MAX_USERS) {
		user_index = 0;
		return false;
	}

	count = 0;
	for (; user_index < MAX_USERS; user_index++) {
		user = &ircd.users[user_index];
		if (!(user->flags & USER_FLAG_CONNECTED)) {
			continue;
		}

		if (!invisible && (user->flags & USER_FLAG_INVISIBLE)) {
			continue;
		}

		if (!(chan->user_flags[user_index] & CHAN_USER_FLAG_JOINED)) {
			continue;
		}

		len = strlen(user->nick);
		if (len + 3 > buflen) {
			break;
		}

		if (count++ > 0) {
			*buf++ = ' ';
			buflen--;
		}

		if (chan->user_flags[user_index] & CHAN_USER_FLAG_CHANOP) {
			*buf++ = '@';
			buflen--;
		} else if (chan->user_flags[user_index] & CHAN_USER_FLAG_VOICE) {
			*buf++ = 'v';
			buflen--;
		}

		strcpy(buf, user->nick);
		buf += len;
		buflen -= len;
	}
	*buf = '\0';

	if (!count) {
		user_index = 0;
	}

	return !!count;
}

static void ICACHE_FLASH_ATTR
ircClientWelcome(IrcUser *user)
{
	char buf[MSGLEN + 1], mode[19];

	snprintf(buf, sizeof(buf), ":%s 001 %s :Welcome to the Internet Relay "
	         "Network %s!%s@" IPSTR, wifi_station_get_hostname(), user->nick,
	         user->nick, user->user, IP2STR(&user->remote_ip));
	ircSend(user, buf);

	snprintf(buf, sizeof(buf), "002 %s :Your host is %s, running version %s",
	         user->nick, wifi_station_get_hostname(), ESPIRCDVERSION);
	ircSend(user, buf);

	snprintf(buf, sizeof(buf), "003 %s :This server was created %s at %s",
	         user->nick, __DATE__, __TIME__);
	ircSend(user, buf);

	snprintf(buf, sizeof(buf), "004 %s :%s %s iow smnt", user->nick,
	         wifi_station_get_hostname(), ESPIRCDVERSION);
	ircSend(user, buf);

	user->flags |= USER_FLAG_REGISTERED | USER_FLAG_WALLOPS |
				   USER_FLAG_INVISIBLE;

	ircUserFlagsToMode(mode, user->flags, user->flags);
	snprintf(buf, sizeof(buf), ":%s MODE %s :%s", user->nick, user->nick,
	         mode);
	ircSend(user, buf);
}

static IrcUser * ICACHE_FLASH_ATTR
ircFindUserByNick(const char *nick)
{
	IrcUser *user;
	int i;

	if (!nick) {
		return NULL;
	}

	for (i = 0; i < MAX_USERS; i++) {
		user = &ircd.users[i];
		if (!(user->flags & USER_FLAG_CONNECTED)) {
			continue;
		}

		if (strncasecmp(user->nick, nick, NICKLEN) != 0) {
			continue;
		}

		return user;
	}
	return NULL;
}

static IrcChan * ICACHE_FLASH_ATTR
ircFindChanByName(const char *name)
{
	IrcChan *chan;
	int i;

	if (!name || name[0] != '#') {
		return NULL;
	}

	for (i = 0; i < MAX_CHANS; i++) {
		chan = &ircd.chans[i];
		if (!chan->users) {
			continue;
		}

		if (strncasecmp(chan->name, name + 1, CHANLEN) != 0) {
			continue;
		}

		return chan;
	}
	return NULL;
}

static IrcChan * ICACHE_FLASH_ATTR
ircCreateChan(const char *name)
{
	IrcChan *chan;
	int i;

	if (!name || name[0] != '#') {
		return NULL;
	}

	for (i = 0; i < MAX_CHANS; i++) {
		chan = &ircd.chans[i];
		if (chan->users) {
			continue;
		}

		break;
	}
	if (i == MAX_CHANS) {
		return NULL;
	}

	bzero(chan, sizeof(IrcChan));

	strncpy(chan->name, name + 1, CHANLEN);
	chan->name[CHANLEN] = '\0';

	return chan;
}

static void ICACHE_FLASH_ATTR
ircJoinChan(IrcUser *joining, const char *name)
{
	IrcChan *chan;
	IrcUser *user;
	char buf[MSGLEN + 1];
	int i;

	chan = ircFindChanByName(name);
	if (chan) {
		if (chan->user_flags[joining->index] & CHAN_USER_FLAG_JOINED) {
			return;
		}
		chan->user_flags[joining->index] = 0;
	}
	if (!chan) {
		chan = ircCreateChan(name);
		if (chan) {
			chan->user_flags[joining->index] |= CHAN_USER_FLAG_CHANOP;
		}
	}
	if (!chan) {
		snprintf(buf, sizeof(buf), "403 %s %s :No such chan", joining->nick,
		         name);
		ircSend(joining, buf);
		return;
	}

	chan->users++;
	chan->user_flags[joining->index] |= CHAN_USER_FLAG_JOINED;

	snprintf(buf, sizeof(buf), ":%s!%s@" IPSTR " JOIN :#%s", joining->nick,
	         joining->user, IP2STR(&joining->remote_ip), chan->name);
	for (i = 0; i < MAX_USERS; i++) {
		user = &ircd.users[i];
		if (!(user->flags & USER_FLAG_CONNECTED)) {
			continue;
		}

		if (!(chan->user_flags[i] & CHAN_USER_FLAG_JOINED)) {
			continue;
		}

		ircSend(user, buf);
	}

	if (chan->topic[0]) {
		snprintf(buf, sizeof(buf), "332 %s #%s :%s", joining->nick,
		         chan->name, chan->topic);
		ircSend(joining, buf);

		// TODO: 333
	}

	i = snprintf(buf, sizeof(buf), "353 %s = #%s :", joining->nick,
	             chan->name);
	while (ircGetChanUsers(chan, buf + i, sizeof(buf) - i, true)) {
		ircSend(joining, buf);
	}

	snprintf(buf, sizeof(buf), "366 %s #%s :End of NAMES list", joining->nick,
	         chan->name);
	ircSend(joining, buf);
}

static void ICACHE_FLASH_ATTR
ircPartChan(IrcUser *leaving, IrcChan *chan, const char *reason)
{
	IrcUser *user;
	char buf[MSGLEN + 1];
	int i, count;

	snprintf(buf, sizeof(buf), ":%s!%s@" IPSTR " PART #%s :%s", leaving->nick,
	         leaving->user, IP2STR(&leaving->remote_ip), chan->name,
	         reason ? reason : "");
	count = 0;
	for (i = 0; i < MAX_USERS; i++) {
		user = &ircd.users[i];
		if (!(user->flags & USER_FLAG_CONNECTED)) {
			continue;
		}

		if (chan->user_flags[i] & CHAN_USER_FLAG_JOINED) {
			count++;
			ircSend(user, buf);
		}
	}

	chan->users--;
	chan->user_flags[leaving->index] &= ~CHAN_USER_FLAG_JOINED;
}

static void ICACHE_FLASH_ATTR
ircAwayCommand(IrcUser *from, IrcMessage *msg)
{
	char buf[MSGLEN + 1];

	if (msg->params < 1 || !msg->param[0][0]) {
		from->flags &= ~USER_FLAG_AWAY;
		snprintf(buf, sizeof(buf), "305 %s :You are no longer marked as "
		         "being away", from->nick);
		ircSend(from, buf);
		return;
	}

	from->flags |= USER_FLAG_AWAY;
	snprintf(buf, sizeof(buf), "306 %s :You have been marked as being away",
	         from->nick);
	ircSend(from, buf);
}

static void ICACHE_FLASH_ATTR
ircInfoCommand(IrcUser *from, IrcMessage *msg)
{
	char buf[MSGLEN + 1];

	if (msg->params >= 1 &&
		(strcasecmp(wifi_station_get_hostname(), msg->param[0]) != 0)) {
		snprintf(buf, sizeof(buf), "402 %s %s :No such server", from->nick,
		         msg->param[0]);
		ircSend(from, buf);
		return;
	}

	snprintf(buf, sizeof(buf), "371 %s :%s", from->nick, ESPIRCDVERSION);
	ircSend(from, buf);

	snprintf(buf, sizeof(buf), "371 %s :Compiled on %s at %s", from->nick,
	         __DATE__, __TIME__);
	ircSend(from, buf);

	snprintf(buf, sizeof(buf), "371 %s :Source code is available at "
	         "https://github.com/jkent/espircd", from->nick);
	ircSend(from, buf);

	snprintf(buf, sizeof(buf), "374 %s :End of INFO list", from->nick);
	ircSend(from, buf);
}

static void ICACHE_FLASH_ATTR
ircJoinCommand(IrcUser *from, IrcMessage *msg)
{
	IrcChan *chan;
	int i;
	char *name = msg->param[0];
	char *p;

	if (name[0] == '0') {
		for (i = 0; i < MAX_CHANS; i++) {
			chan = &ircd.chans[i];
			if (!chan->users) {
				continue;
			}
			
			if (chan->user_flags[from->index] & CHAN_USER_FLAG_JOINED) {
				ircPartChan(from, chan, "Left all channels");
			}
		}
		return;
	}

	p = name;
	while (*p++) {
		if (*p == ',') {
			*p++ = '\0';
			ircJoinChan(from, name);
			name = p;
		}
	}
	ircJoinChan(from, name);
}

static void ICACHE_FLASH_ATTR
ircLusersCommand(IrcUser *from, IrcMessage *msg)
{
	IrcUser *user;
	IrcChan *chan;
	char buf[MSGLEN + 1];
	int i;
	int users, invisible, operators;
	int chans;
	
	if (msg->params >= 1 &&
		(strcasecmp(wifi_station_get_hostname(), msg->param[0]) != 0)) {
		snprintf(buf, sizeof(buf), "402 %s %s :No such server", from->nick,
		         msg->param[0]);
		ircSend(from, buf);
		return;
	}

	users = 0;
	invisible = 0;
	operators = 0;
	for (i = 0; i < MAX_USERS; i++) {
		user = &ircd.users[i];
		if (!(user->flags & USER_FLAG_CONNECTED)) {
			continue;
		}

		if (!(user->flags & USER_FLAG_INVISIBLE)) {
			users++;
		} else {
			invisible++;
		}

		if (user->flags & USER_FLAG_OPERATOR) {
			operators++;
		}
	}

	chans = 0;
	for (i = 0; i < MAX_CHANS; i++) {
		chan = &ircd.chans[i];
		if (chan->users) {
			chans++;
		}
	}

	snprintf(buf, sizeof(buf), "251 %s :There are %d users and %d invisible "
	         "on 1 servers", from->nick, users, invisible);
	ircSend(from, buf);

	snprintf(buf, sizeof(buf), "252 %s %d :operator(s) online", from->nick,
	         operators);
	ircSend(from, buf);

	snprintf(buf, sizeof(buf), "254 %s %d :channels formed", from->nick,
	         chans);
	ircSend(from, buf);

	snprintf(buf, sizeof(buf), "255 %s :I have %d clients and 0 servers",
	         from->nick, users + invisible);
	ircSend(from, buf);
}

static void ICACHE_FLASH_ATTR
ircModeCommand(IrcUser *from, IrcMessage *msg)
{
	char buf[MSGLEN + 1], mode[19];
	char *p;
	char operator;
	uint16 state;
	bool unknown;

	if (msg->param[0][0] == '#') {
		//ircChanModeCommand(from, buf);
		return;
	}

	if (strcasecmp(from->nick, msg->param[0]) != 0) {
		snprintf(buf, sizeof(buf), "502 %s :Cannot change mode for other "
		         "users", from->nick);
		ircSend(from, buf);
		return;
	}

	if (msg->params < 2) {
		ircUserFlagsToMode(mode, from->flags, from->flags);
		snprintf(buf, sizeof(buf), "221 %s %s", from->nick, mode);
		ircSend(from, buf);
		return;
	}	

	p = msg->param[1];
	operator = '\0';
	state = from->flags;
	unknown = false;
	while (*p) {
		if (*p == '+' || *p == '-') {
			operator = *p++;
			continue;
		}

		switch (*p++) {
		case 'a':
			break;
		case 'w':
			if (operator == '+') {
				state |= USER_FLAG_WALLOPS;
			} else if (operator == '-') {
				state &= ~USER_FLAG_WALLOPS;
			}
			break;
		case 'i':
			if (operator == '+') {
				state |= USER_FLAG_INVISIBLE;
			} else if (operator == '-') {
				state &= ~USER_FLAG_INVISIBLE;
			}
			break;
		case 'o':
			if (operator == '-') {
				state &= ~USER_FLAG_OPERATOR;
			}
			break;
		default:
			unknown = true;
			break;
		}
	}

	if (unknown) {
		snprintf(buf, sizeof(buf), "501 %s :Unknown MODE flag", from->nick);
		ircSend(from, buf);
	}

	if (state ^ from->flags) {
		ircUserFlagsToMode(mode, state, state ^ from->flags);
		snprintf(buf, sizeof(buf), ":%s MODE %s :%s", from->nick, from->nick,
		         mode);
		ircSend(from, buf);
		from->flags = state;
	}
}

static void ICACHE_FLASH_ATTR
ircNamesCommand(IrcUser *from, IrcMessage *msg)
{
	IrcChan *chan = ircFindChanByName(msg->param[0]);
	char buf[MSGLEN + 1];
	bool joined;
	int i;

	if (chan) {
		joined = (chan->user_flags[from->index] & CHAN_USER_FLAG_JOINED);

		i = snprintf(buf, sizeof(buf), "353 %s = #%s :", from->nick,
		             chan->name);
		while (ircGetChanUsers(chan, buf + i, sizeof(buf) - i, joined)) {
			ircSend(from, buf);
		}

		snprintf(buf, sizeof(buf), "366 %s #%s :End of NAMES list",
		         from->nick, chan->name);
		ircSend(from, buf);
		return;
	}

	snprintf(buf, sizeof(buf), "366 %s %s :End of NAMES list", from->nick,
	         msg->param[0] ? msg->param[0] : "*");
	ircSend(from, buf);	

}

static void ICACHE_FLASH_ATTR
ircMotdCommand(IrcUser *from, IrcMessage *msg)
{
	char buf[MSGLEN + 1];

	if (msg->params >= 1 &&
		(strcasecmp(wifi_station_get_hostname(), msg->param[0]) != 0)) {
		snprintf(buf, sizeof(buf), "402 %s %s :No such server", from->nick,
		         msg->param[0]);
		ircSend(from, buf);
		return;
	}

	snprintf(buf, sizeof(buf), "422 %s :MOTD File is missing", from->nick);
	ircSend(from, buf);
}

static void ICACHE_FLASH_ATTR
ircNickCommand(IrcUser *from, IrcMessage *msg)
{
	IrcUser *user;
	char buf[MSGLEN + 1];
	char *p;
	bool valid = true;
	char oldnick[NICKLEN + 1];

	if (msg->params < 1) {
		snprintf(buf, sizeof(buf), "431 %s :No nickname given", from->nick);
		ircSend(from, buf);
		return;
	}

	p = msg->param[0];
	if (*p < 'A' || *p > '}') {
		valid = false;
	}
	p++;
	while (valid && *p) {
		if (*p != '-' && (*p < '0' || (*p > '9' && *p < 'A') || *p > '}')) {
			valid = false;
		}
		p++;
	}
	if (!valid) {
		snprintf(buf, sizeof(buf), "432 %s %s :Erroneous Nickname: Illegal "
		         "characters", from->nick, msg->param[0]);
		ircSend(from, buf);
		return;
	}

	user = ircFindUserByNick(msg->param[0]);
	if (user) {
		snprintf(buf, sizeof(buf), "433 %s %s :Nickname is already in use.",
		         from->nick[0] ? from->nick : "*", msg->param[0]);
		ircSend(from, buf);
		return;
	}

	strcpy(oldnick, from->nick);
	strncpy(from->nick, msg->param[0], NICKLEN);
	from->nick[NICKLEN] = '\0';

	if (!(from->flags & USER_FLAG_REGISTERED) && from->user[0]) {
		ircClientWelcome(from);
		return;
	}

	snprintf(buf, sizeof(buf), ":%s!%s@" IPSTR " NICK :%s", oldnick,
	         from->user, IP2STR(&from->remote_ip), from->nick);
	ircBroadcast(from, buf);
}

static void ICACHE_FLASH_ATTR
ircNoticeCommand(IrcUser *from, IrcMessage *msg)
{
	IrcChan *chan;
	IrcUser *user;
	char buf[MSGLEN + 1];
	bool joined;
	int i;

	if (msg->params < 1) {
		snprintf(buf, sizeof(buf), "411 %s :No recipient given (NOTICE)",
		         from->nick);
		ircSend(from, buf);
		return;
	}

	if (msg->params < 2 || !msg->param[1][0]) {
		snprintf(buf, sizeof(buf), "412 %s :No text to send", from->nick);
		ircSend(from, buf);
		return;
	}

	chan = ircFindChanByName(msg->param[0]);
	if (chan) {
		joined = (chan->user_flags[from->index] & CHAN_USER_FLAG_JOINED);
		if (!joined && (chan->flags & CHAN_FLAG_NOOUTSIDE)) {
			return;
		}

		snprintf(buf, sizeof(buf), ":%s!%s@" IPSTR " NOTICE %s :%s",
		         from->nick, from->user, IP2STR(&from->remote_ip),
		         msg->param[0], msg->param[1]);
		for (i = 0; i < MAX_USERS; i++) {
			user = &ircd.users[i];
			if (!(user->flags & USER_FLAG_CONNECTED)) {
				continue;
			}

			if (!(chan->user_flags[i] & CHAN_USER_FLAG_JOINED)) {
				continue;
			}

			if (user == from) {
				continue;
			}

			ircSend(&ircd.users[i], buf);
		}
		return;
	}

	user = ircFindUserByNick(msg->param[0]);
	if (!user) {
		snprintf(buf, sizeof(buf), "401 %s %s :No such nick/channel",
		         from->nick, msg->param[0]);
		ircSend(from, buf);
		return;
	}

	snprintf(buf, sizeof(buf), ":%s!%s@" IPSTR " NOTICE %s :%s", from->nick,
	         from->user, IP2STR(&from->remote_ip), msg->param[0],
	         msg->param[1]);
	ircSend(user, buf);
}

static void ICACHE_FLASH_ATTR
ircOperCommand(IrcUser *from, IrcMessage *msg)
{
	char buf[MSGLEN + 1];

	if (strcmp(msg->param[0], OPER_NAME) != 0) {
		snprintf(buf, sizeof(buf), "491 %s :No O-lines for your host",
		         from->nick);
		ircSend(from, buf);
		return;
	}

	if (strcmp(msg->param[1], OPER_PASSWORD) != 0) {
		snprintf(buf, sizeof(buf), "464 %s :Password incorrect", from->nick);
		ircSend(from, buf);
		return;
	}

	if (!(from->flags & USER_FLAG_OPERATOR)) {
		from->flags |= USER_FLAG_OPERATOR;
		snprintf(buf, sizeof(buf), ":%s MODE %s :+o", from->nick, from->nick);
		ircSend(from, buf);
	}

	snprintf(buf, sizeof(buf), "381 %s :You are now an IRC Operator",
	         from->nick);
	ircSend(from, buf);
}

static void ICACHE_FLASH_ATTR
ircPartCommand(IrcUser *from, IrcMessage *msg)
{
	IrcChan *chan;
	char buf[MSGLEN + 1];
	char *reason = NULL;

	chan = ircFindChanByName(msg->param[0]);
	if (!chan) {
		snprintf(buf, sizeof(buf), "403 %s %s :No such channel", from->nick,
		         msg->param[0]);
		ircSend(from, buf);
		return;
	}

	if (!(chan->user_flags[from->index] & CHAN_USER_FLAG_JOINED)) {
		snprintf(buf, sizeof(buf), "442 %s #%s :You're not on that channel",
		         from->nick, chan->name);
		ircSend(from, buf);
		return;
	}

	if (msg->params > 1) {
		reason = msg->param[1];
	}

	ircPartChan(from, chan, reason);
}

static void ICACHE_FLASH_ATTR
ircPingCommand(IrcUser *from, IrcMessage *msg)
{
	char buf[MSGLEN + 1];

	if (msg->params < 1) {
		snprintf(buf, sizeof(buf), "409 %s :No origin specified", from->nick);
		ircSend(from, buf);
		return;
	}

	snprintf(buf, sizeof(buf), "PONG %s :%s", wifi_station_get_hostname(),
	         msg->param[0]);
	ircSend(from, buf);
}

static void ICACHE_FLASH_ATTR
ircPongCommand(IrcUser *from, IrcMessage *msg)
{
	char buf[MSGLEN + 1];

	if (msg->params < 1) {
		snprintf(buf, sizeof(buf), "409 %s :No origin specified", from->nick);
		ircSend(from, buf);
		return;
	}
}

static void ICACHE_FLASH_ATTR
ircPrivmsgCommand(IrcUser *from, IrcMessage *msg)
{
	IrcChan *chan;
	IrcUser *user;
	char buf[MSGLEN + 1];
	bool joined;
	int i;

	if (msg->params < 1) {
		snprintf(buf, sizeof(buf), "411 %s :No recipient given (PRIVMSG)",
		         from->nick);
		ircSend(from, buf);
		return;
	}

	if (msg->params < 2 || !msg->param[1][0]) {
		snprintf(buf, sizeof(buf), "412 %s :No text to send", from->nick);
		ircSend(from, buf);
		return;
	}

	chan = ircFindChanByName(msg->param[0]);
	if (chan) {
		joined = (chan->user_flags[from->index] & CHAN_USER_FLAG_JOINED);
		if (!joined && (chan->flags & CHAN_FLAG_NOOUTSIDE)) {
			snprintf(buf, sizeof(buf), "404 %s #%s :No external channel "
			         "messages (#%s)", from->nick, chan->name, chan->name);
			ircSend(from, buf);
			return;
		}

		snprintf(buf, sizeof(buf), ":%s!%s@" IPSTR " PRIVMSG %s :%s",
		         from->nick, from->user, IP2STR(&from->remote_ip),
		         msg->param[0], msg->param[1]);
		for (i = 0; i < MAX_USERS; i++) {
			user = &ircd.users[i];
			if (!(user->flags & USER_FLAG_CONNECTED)) {
				continue;
			}

			if (!(chan->user_flags[i] & CHAN_USER_FLAG_JOINED)) {
				continue;
			}

			if (user == from) {
				continue;
			}

			ircSend(user, buf);
		}
		return;
	}

	user = ircFindUserByNick(msg->param[0]);
	if (!user) {
		snprintf(buf, sizeof(buf), "401 %s %s :No such nick/channel",
		         from->nick, msg->param[0]);
		ircSend(from, buf);
		return;
	}

	if (user->flags & USER_FLAG_AWAY) {
		snprintf(buf, sizeof(buf), "301 %s %s :User is currently away",
		         from->nick, user->nick);
		ircSend(from, buf);
	}

	snprintf(buf, sizeof(buf), ":%s!%s@" IPSTR " PRIVMSG %s :%s", from->nick,
	         from->user, IP2STR(&from->remote_ip), msg->param[0],
	         msg->param[1]);
	ircSend(user, buf);
}

static void ICACHE_FLASH_ATTR
ircQuitCommand(IrcUser *from, IrcMessage *msg)
{
	char *reason = from->nick;

	if (msg->params >= 1) {
		reason = msg->param[0];
	}

	ircDisconnect(from, "QUIT", "Quit: ", reason);
}

static void ICACHE_FLASH_ATTR
ircTopicCommand(IrcUser *from, IrcMessage *msg)
{
	IrcUser *user;
	IrcChan *chan = ircFindChanByName(msg->param[0]);
	char buf[MSGLEN + 1];
	bool joined;
	bool privileged;
	int i;

	if (!chan) {
		snprintf(buf, sizeof(buf), "403 %s %s :No such channel", from->nick,
		         msg->param[0]);
		ircSend(from, buf);
		return;
	}

	joined = (chan->user_flags[from->index] & CHAN_USER_FLAG_JOINED);

	if (msg->params < 2) {
		if (!joined && (chan->flags & CHAN_FLAG_SECRET)) {
			snprintf(buf, sizeof(buf), "442 %s #%s :You're not on that "
			         "channel", from->nick, chan->name);
			ircSend(from, buf);
			return;
		}

		if (!chan->topic[0]) {
			snprintf(buf, sizeof(buf), "331 %s #%s :No topic is set.",
			         from->nick, chan->name);
			ircSend(from, buf);
			return;
		}
		
		snprintf(buf, sizeof(buf), "332 %s #%s :%s", from->nick, chan->name,
		         chan->topic);
		ircSend(from, buf);
		
		// TODO: 333

		return;
	}

	if (!joined) {
		snprintf(buf, sizeof(buf), "442 %s #%s :You're not on that channel",
		         from->nick, chan->name);
		ircSend(from, buf);
		return;
	}

	privileged = (chan->user_flags[from->index] & CHAN_USER_FLAG_CHANOP) ||
				 (from->flags & USER_FLAG_OPERATOR);

	if (!privileged && (chan->flags & CHAN_FLAG_TOPICLOCK)) {
		snprintf(buf, sizeof(buf), "482 %s #%s :You're not channel operator",
		         from->nick, chan->name);
		ircSend(from, buf);
		return;
	}

	strncpy(chan->topic, msg->param[1], TOPICLEN);
	chan->topic[TOPICLEN] = '\0';

	snprintf(buf, sizeof(buf), ":%s!%s@" IPSTR " TOPIC #%s :%s", from->nick,
	         from->user, IP2STR(&from->remote_ip), chan->name, chan->topic);
	for (i = 0; i < MAX_USERS; i++) {
		user = &ircd.users[i];
		if (!(user->flags & USER_FLAG_CONNECTED)) {
			continue;
		}

		if (!(chan->user_flags[i] & CHAN_USER_FLAG_JOINED)) {
			continue;
		}

		ircSend(user, buf);
	}
}

static void ICACHE_FLASH_ATTR
ircUserCommand(IrcUser *from, IrcMessage *msg)
{
	char buf[MSGLEN + 1];

	if (from->flags & USER_FLAG_REGISTERED) {
		snprintf(buf, sizeof(buf), "462 %s :You may not reregister",
		         from->nick);
		ircSend(from, buf);
		return;
	}

	strncpy(from->user, msg->param[0], USERLEN);
	from->user[USERLEN] = '\0';

	strncpy(from->real, msg->param[3], REALLEN);
	from->real[REALLEN] = '\0';

	if (!(from->flags & USER_FLAG_REGISTERED) && from->nick[0]) {
		ircClientWelcome(from);
	}
}

static void ICACHE_FLASH_ATTR
ircVersionCommand(IrcUser *from, IrcMessage *msg)
{
	char buf[MSGLEN + 1];

	if (msg->params >= 1 &&
		(strcasecmp(wifi_station_get_hostname(), msg->param[0]) != 0)) {
		snprintf(buf, sizeof(buf), "402 %s %s :No such server", from->nick,
		         msg->param[0]);
		ircSend(from, buf);
		return;
	}

	snprintf(buf, sizeof(buf), "351 %s %s. %s :Running on an ESP8266 wifi "
	         "module!", from->nick, ESPIRCDVERSION,
	         wifi_station_get_hostname());
	ircSend(from, buf);
}

static void ICACHE_FLASH_ATTR
ircWallopsCommand(IrcUser *from, IrcMessage *msg)
{
	IrcUser *user;
	char buf[MSGLEN + 1];
	int i;

	if (!(from->flags & USER_FLAG_OPERATOR)) {
		snprintf(buf, sizeof(buf), "481 %s :Permission Denied- You're not an "
		         "IRC operator", from->nick);
		ircSend(from, buf);
		return;
	}

	snprintf(buf, sizeof(buf), ":%s!%s@" IPSTR " WALLOPS :%s", from->nick,
	         from->user, IP2STR(&from->remote_ip), msg->param[0]);
	for (i = 0; i < MAX_USERS; i++) {
		user = &ircd.users[i];
		if (!(user->flags & USER_FLAG_CONNECTED)) {
			continue;
		}

		if (!(from->flags & USER_FLAG_WALLOPS)) {
			continue;
		}

		ircSend(user, buf);
	}
}

static void ICACHE_FLASH_ATTR
ircWhoCommand(IrcUser *from, IrcMessage *msg)
{
	IrcUser *user;
	char buf[MSGLEN + 1], flags[4];
	char *p;
	int i;

	if (msg->params < 1) {
		for (i = 0; i < MAX_USERS; i++) {
			user = &ircd.users[i];
			if (!(user->flags & USER_FLAG_CONNECTED)) {
				continue;
			}

			if (!(from->flags & USER_FLAG_OPERATOR) && (user != from) &&
				(user->flags & USER_FLAG_INVISIBLE)) {
				continue;
			}

			p = flags;
			*p++ = (user->flags & USER_FLAG_AWAY) ? 'G' : 'H';
			if (user->flags & USER_FLAG_OPERATOR) {
				*p++ = '*';
			}
			*p = '\0';
			snprintf(buf, sizeof(buf), "352 %s * %s " IPSTR " %s %s %s :0 %s",
			         from->nick, user->user, IP2STR(&user->remote_ip),
			         wifi_station_get_hostname(), user->nick, flags,
			         user->real);
			ircSend(from, buf);
		}
		snprintf(buf, sizeof(buf), "315 %s * :End of WHO list", from->nick);
		ircSend(from, buf);
		return;
	}

	if (msg->param[0][0] == '#') {
		// search chans
	} else {
		user = ircFindUserByNick(msg->param[0]);
		if (user) {
			p = flags;
			*p++ = (user->flags & USER_FLAG_AWAY) ? 'G' : 'H';
			if (user->flags & USER_FLAG_OPERATOR) {
				*p++ = '*';
			}
			*p = '\0';
			snprintf(buf, sizeof(buf), "352 %s * %s " IPSTR " %s %s %s :0 %s",
			         from->nick, user->user, IP2STR(&user->remote_ip),
			         wifi_station_get_hostname(), user->nick, flags,
			         user->real);
			ircSend(from, buf);
		}
	}
	snprintf(buf, sizeof(buf), "315 %s %s :End of WHO list", msg->param[0],
	         from->nick);
	ircSend(from, buf);
}

static void ICACHE_FLASH_ATTR
ircWhoisCommand(IrcUser *from, IrcMessage *msg)
{
	IrcUser *user;
	char buf[MSGLEN + 1], mode[19];
	int i;

	if (msg->params < 1) {
		snprintf(buf, sizeof(buf), "431 %s :No nickname given", from->nick);
		ircSend(from, buf);
		return;
	}

	user = ircFindUserByNick(msg->param[0]);
	if (user) {
		snprintf(buf, sizeof(buf), "311 %s %s %s " IPSTR " * :%s", from->nick,
		         user->nick, user->user, IP2STR(&user->remote_ip),
		         user->real);
		ircSend(from, buf);

		if (from->flags & USER_FLAG_OPERATOR) {
			ircUserFlagsToMode(mode, user->flags, user->flags);
			snprintf(buf, sizeof(buf), "379 %s %s :is using modes %s",
			         from->nick, user->nick, mode);
			ircSend(from, buf);

			snprintf(buf, sizeof(buf), "378 %s %s :is connecting from *@"
			         IPSTR, from->nick, user->nick, IP2STR(&user->remote_ip));
			ircSend(from, buf);
		}

		i = snprintf(buf, sizeof(buf), "319 %s %s :", from->nick, user->nick);
		while (ircGetUserChans(user, buf + i, sizeof(buf) - i)) {
			ircSend(from, buf);
		}

		snprintf(buf, sizeof(buf), "312 %s %s %s :ESP8266 network",
		         from->nick, user->nick, wifi_station_get_hostname());
		ircSend(from, buf);

		if (user->flags & USER_FLAG_OPERATOR) {
			snprintf(buf, sizeof(buf), "313 %s %s :is an IRC Operator",
			         from->nick, user->nick);
			ircSend(from, buf);
		}
		
		// TODO: 317 idle
	} else {
		snprintf(buf, sizeof(buf), "401 %s %s :No such nick/channel",
		         from->nick, msg->param[0]);
		ircSend(from, buf);
	}

	snprintf(buf, sizeof(buf), "318 %s %s :End of WHOIS list", msg->param[0],
	         from->nick);
	ircSend(from, buf);
}

static IrcCommand userCommands[] = {
	{"AWAY",    0, false, ircAwayCommand   },
	{"INFO",    0, false, ircInfoCommand   },
	{"JOIN",    1, false, ircJoinCommand   },
	{"LUSERS",  0, false, ircLusersCommand },
	{"MODE",    1, false, ircModeCommand   },
	{"MOTD",    0, false, ircMotdCommand   },
	{"NAMES",   0, false, ircNamesCommand  },
	{"NICK",    0, true,  ircNickCommand   },
	{"NOTICE",  0, false, ircNoticeCommand },
	{"OPER",    2, false, ircOperCommand   },
	{"PART",    1, false, ircPartCommand   },
	{"PING",    0, false, ircPingCommand   },
	{"PONG",    0, false, ircPongCommand   },
	{"PRIVMSG", 0, false, ircPrivmsgCommand},
	{"QUIT",    0, true,  ircQuitCommand   },
	{"TOPIC",   1, false, ircTopicCommand  },
	{"USER",    4, true,  ircUserCommand   },
	{"VERSION", 0, false, ircVersionCommand},
	{"WALLOPS", 1, false, ircWallopsCommand},
	{"WHO",     0, false, ircWhoCommand    },
	{"WHOIS",   0, false, ircWhoisCommand  },
	{NULL,      0, false, NULL             },
};

static void ICACHE_FLASH_ATTR
ircClientCommand(IrcUser *user, IrcMessage *msg)
{
	IrcCommand *cmd = userCommands;
	char buf[MSGLEN + 1];

	while (cmd->name) {
		if (strcmp(msg->cmd, cmd->name) != 0) {
			cmd++;
			continue;
		}

		if (!(user->flags & USER_FLAG_REGISTERED) & !cmd->for_registration) {
			break;
		}

		if (cmd->required_params && ((cmd->required_params > msg->params) ||
			!msg->param[cmd->required_params - 1][0])) {
			snprintf(buf, sizeof(buf), "461 %s %s :Not enough parameters",
			         user->nick, msg->cmd);
			ircSend(user, buf);
			return;
		}

		cmd->handler(user, msg);
		return;
	}

	if (!(user->flags & USER_FLAG_REGISTERED)) {
		snprintf(buf, sizeof(buf), "451 %s :You have not registered",
		         msg->cmd);
		ircSend(user, buf);
	} else {
		snprintf(buf, sizeof(buf), "421 %s :Unknown command", msg->cmd);
		ircSend(user, buf);
	}
}

static bool ICACHE_FLASH_ATTR
ircParse(char *s, IrcMessage *msg)
{
	char *p = s;

	bzero(msg, sizeof(IrcMessage));

	if (*p == ':') {
		msg->prefix = ++p;
	} else if (*p) {
		msg->cmd = p;
		if (*p >= 'a' && *p <= 'z') {
			*p -= 32;
		}
		p++;
	}

	while (*p) {
		if (*p == ' ') {
			*p++ = '\0';
			continue;
		}

		if (*(p - 1) == '\0') {
			if (!msg->cmd) {
				msg->cmd = p;
			} else {
				if (*p == ':') {
					msg->param[msg->params++] = p + 1;
					break;
				} else {
					msg->param[msg->params++] = p;
					if (msg->params >= MAX_PARAM) {
						break;
					}
				}
			}
		}

		if (msg->cmd && msg->params == 0) {
			if (*p >= 'a' && *p <= 'z') {
				*p -= 32;
			}
		}
		p++;
	}

	return !!msg->cmd;
}

static void ICACHE_FLASH_ATTR
ircdTimerCb(void *arg)
{
	int i;
	char buf[MSGLEN + 1];
	IrcUser *user;

	for (i = 0; i < MAX_USERS; i++) {
		user = &ircd.users[i];
		if (!(user->flags & USER_FLAG_CONNECTED)) {
			continue;
		}

		if (!(user->flags & USER_FLAG_REGISTERED) &&
			(user->last_recv >= UNREGISTERED_TIMEOUT)) {
			snprintf(buf, sizeof(buf), "Ping timeout: %d seconds",
			         user->last_recv);
			ircDisconnect(user, "QUIT", NULL, buf);
		}

		if (user->sent_ping && user->last_recv < PING_TIME) {
			user->sent_ping = false;
		}

		if (!user->sent_ping && user->last_recv >= PING_TIME) {
			snprintf(buf, sizeof(buf), "PING :%s",
			         wifi_station_get_hostname());
			ircSend(user, buf);
			user->sent_ping = true;
		}
		
		if (user->last_recv >= PING_TIMEOUT) {
			snprintf(buf, sizeof(buf), "Ping timeout: %d seconds",
			         user->last_recv);
			ircDisconnect(user, "QUIT", NULL, buf);
		}

		user->last_recv++;
	}
}

static IrcUser * ICACHE_FLASH_ATTR
ircdFindUserData(struct espconn *conn)
{
	int i;
	IrcUser *user;

	for (i = 0; i < MAX_USERS; i++) {
		user = &ircd.users[i];
		if (!(user->flags & USER_FLAG_CONNECTED)) {
			continue;
		}

		if ((memcmp(user->remote_ip, conn->proto.tcp->remote_ip, 4) == 0) &&
		    (user->remote_port == conn->proto.tcp->remote_port)) {
			return user;
		}
	}

	return NULL;
}

static void ICACHE_FLASH_ATTR
ircdClientRecvCb(struct espconn *conn, char *data, unsigned short len)
{
	char c;
	char *dst, *src;
	IrcUser *user;
	IrcMessage msg;

	user = ircdFindUserData(conn);
	if (!user) {
		return;
	}

	user->last_recv = 0;

	src = data;
	dst = user->msgbuf + strlen(user->msgbuf);

	while (src < data + len) {
		c = *src++;
		if (c == 0 || c == '\r') {
			continue;
		}

		if (c == '\n') {
			*dst = 0;
			dst = user->msgbuf;

#ifdef DEBUG
			printf("u%2d >> %s\n", user->index, user->msgbuf);
#endif

			if (ircParse(user->msgbuf, &msg)) {
				ircClientCommand(user, &msg);
			}
			continue;
		}

		if (dst < user->msgbuf + MSGLEN) {
			*dst++ = c;
		}
	}
	*dst = 0;
}

static void ICACHE_FLASH_ATTR
ircdClientDisconnectCb(struct espconn *conn)
{
	IrcUser *user = ircdFindUserData(conn);

	if (!user) {
		return;
	}

	user->flags &= ~USER_FLAG_CONNECTED;
#ifdef DEBUG
	printf("u%2d disconnected\n", user->index);
#endif
	ircDisconnect(user, "QUIT", NULL, "Client exited");
}

static void ICACHE_FLASH_ATTR
ircdClientConnectCb(struct espconn *conn)
{
	IrcUser *user;
	int i;

	for (i = 0; i < MAX_USERS; i++) {
		user = &ircd.users[i];
		if (!(user->flags & USER_FLAG_CONNECTED)) {
			break;
		}
	}
	if (i == MAX_USERS) {
#ifdef DEBUG
		printf("u-1 << %s", "ERROR :SERVER IS FULL\n");
#endif
		espconn_sent(conn, (unsigned char *)"ERROR :SERVER IS FULL\r\n", 23);
		espconn_disconnect(conn);
#ifdef DEBUG
		printf("u-1 disconnected\n");
#endif
		return;
	}

	bzero(user, sizeof(IrcUser));
	memcpy(user->remote_ip, conn->proto.tcp->remote_ip, 4);
	user->remote_port = conn->proto.tcp->remote_port;
	user->flags |= USER_FLAG_CONNECTED;
	user->index = i;

	espconn_regist_recvcb(conn, (void (*)(void *, char *, unsigned short))
						  ircdClientRecvCb);
	espconn_regist_disconcb(conn, (void (*)(void *))ircdClientDisconnectCb);

#ifdef DEBUG
	printf("u%2d connected\n", i);
#endif
}

void ICACHE_FLASH_ATTR
ircdInit(int port)
{
	bzero(&ircd, sizeof(ircd));

	ircd.conn.type = ESPCONN_TCP;
	ircd.conn.state = ESPCONN_NONE;
	ircd.tcp.local_port = port;
	ircd.conn.proto.tcp = &ircd.tcp;

	espconn_tcp_set_max_con(MAX_USERS + 1);
	espconn_regist_connectcb(&ircd.conn, (void (*)(void *))
							 ircdClientConnectCb);
	espconn_accept(&ircd.conn);
	espconn_regist_time(&ircd.conn, PING_TIMEOUT + 60, 0);
	espconn_tcp_set_max_con_allow(&ircd.conn, MAX_USERS + 1);

	os_timer_disarm(&ircd.timer);
	os_timer_setfn(&ircd.timer, ircdTimerCb, NULL);
	os_timer_arm(&ircd.timer, 1000, 1);
}

