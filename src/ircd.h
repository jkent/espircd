#ifndef IRCD_H
#define IRCD_H

#define ESPIRCDVERSION "espircd0.1"

#define OPER_NAME "name"
#define OPER_PASSWORD "password"

// SDK limit of 15 connections, but we can run out of heap with just 6
#define MAX_USERS 5
#define MAX_CHANS 4
#define MAX_PARAM 15

#define MSGLEN 510
#define USERLEN 10
#define NICKLEN 9
#define REALLEN 50
#define CHANLEN 49
#define TOPICLEN 307

#define UNREGISTERED_TIMEOUT 30
#define PING_TIME 90
#define PING_TIMEOUT 180

#define USER_FLAG_CONNECTED  0x0001
#define USER_FLAG_REGISTERED 0x0002
#define USER_FLAG_AWAY       0x0004
#define USER_FLAG_WALLOPS    0x0008
#define USER_FLAG_INVISIBLE  0x0010
#define USER_FLAG_OPERATOR   0x0020

#define CHAN_FLAG_SECRET     0x0001
#define CHAN_FLAG_MODERATED  0x0002
#define CHAN_FLAG_NOOUTSIDE  0x0004
#define CHAN_FLAG_TOPICLOCK  0x0008

#define CHAN_USER_FLAG_JOINED  0x01
#define CHAN_USER_FLAG_INVITED 0x02
#define CHAN_USER_FLAG_CHANOP  0x04
#define CHAN_USER_FLAG_VOICE   0x08

typedef struct Ircd Ircd;
typedef struct IrcUser IrcUser;
typedef struct IrcChan IrcChan;
typedef struct IrcMessage IrcMessage;
typedef struct IrcCommand IrcCommand;

struct IrcUser {
	uint8 remote_ip[4];
	uint16 remote_port;
	unsigned char index;
	char msgbuf[MSGLEN + 1];
	char user[USERLEN + 1];
	char nick[NICKLEN + 1];
	char real[REALLEN + 1];
	uint16 flags;
	unsigned char last_recv;
	bool sent_ping;
};

struct IrcChan {
	char name[CHANLEN + 1];
	char topic[TOPICLEN + 1];
	unsigned char users;
	uint8 user_flags[MAX_USERS];
	uint16 flags;
};

struct Ircd {
	struct espconn conn;
	esp_tcp tcp;
	ETSTimer timer;
	IrcUser users[MAX_USERS];
	IrcChan chans[MAX_CHANS];
};

struct IrcMessage {
	char *prefix;
	char *cmd;
	int params;
	char *param[MAX_PARAM];
};

struct IrcCommand {
	char *name;
	unsigned char required_params;
	bool for_registration;
	void (*handler)(IrcUser *client, IrcMessage *msg);
};

void ICACHE_FLASH_ATTR ircdInit(int port);

#endif /* IRCD_H */
