bin_PROGRAMS          = dhcprelayd

dhcprelayd_CFLAGS     = -W -Wall -Wextra -fno-strict-aliasing
dhcprelayd_CFLAGS    += $(CONFUSE_CFLAGS)
dhcprelayd_LDADD      = $(CONFUSE_LIBS)
dhcprelayd_SOURCES    = dhcprelayd.c options.h queue.h conf.c conf.h	\
			packet.c packet.h options.c logging.c logging.h	\
			misc.c misc.h
