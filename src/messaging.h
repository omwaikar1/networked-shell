/*
 * messaging.h - Inter-session messaging built-ins.
 *
 * Author : Sakshi Takalkar
 */
#ifndef NCS_MESSAGING_H
#define NCS_MESSAGING_H

#include "session.h"

/* All four are registered in commands.c's builtins[] table. */
int builtin_nick     (Session *s, int argc, char **argv);
int builtin_who      (Session *s, int argc, char **argv);
int builtin_msg      (Session *s, int argc, char **argv);
int builtin_broadcast(Session *s, int argc, char **argv);

#endif /* NCS_MESSAGING_H */
