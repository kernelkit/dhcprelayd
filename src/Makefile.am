bin_PROGRAMS          = dhcprelayd

dhcprelayd_CFLAGS     = -W -Wall -Wextra -fno-strict-aliasing
dhcprelayd_CFLAGS    += $(JANSSON_CFLAGS) $(LIBNL_CFLAGS)
dhcprelayd_LDADD      = $(JANSSON_LIBS)   $(LIBNL_LIBS)
dhcprelayd_SOURCES    = dhcprelayd.c options.h queue.h conf.c conf.h	\
			packet.c packet.h options.c logging.c logging.h	\
			misc.c misc.h
