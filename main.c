/*
    This file is part of telegram-cli.

    Telegram-cli is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Telegram-cli is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this telegram-cli.  If not, see <http://www.gnu.org/licenses/>.

    Copyright Vitaly Valtman 2013-2015
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <termios.h>
#include <unistd.h>
#include <assert.h>

#ifdef __FreeBSD__
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#ifdef READLINE_GNU
#include <readline/readline.h>
#else
#include <editline/readline.h>
#include <locale.h>
#endif

#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <fcntl.h>

#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif
#include <signal.h>
#ifdef HAVE_LIBCONFIG
#include <libconfig.h>
#endif

#include <grp.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <getopt.h>

#include "auto/git_info.h"

#ifdef USE_LUA
#  include "lua-tg.h"
#endif

#include "interface.h"
#include "loop.h"
#include "telegram.h"

#define PROGNAME "telegram-cli"
#define VERSION "0.07"

#define CONFIG_DIRECTORY "." PROG_NAME
#define CONFIG_FILE "config"


#define CONFIG_DIRECTORY_MODE 0700

#define DEFAULT_CONFIG_CONTENTS     \
  "# This is an empty config file\n" \
  "# Feel free to put something here\n"

struct TdCClientParameters params = {
  .is_test_dc = 0,
  .api_id = TELEGRAM_CLI_API_ID,
  .api_hash = TELEGRAM_CLI_API_HASH,
  .language_code = "en",
  .device_model = "PC",
  .system_version = "Unix/??",
  .app_version = TELEGRAM_CLI_VERSION,
  .use_secret_chats = 0,
  .database_directory = ".",
  .files_directory = ".",
  
  .updates_handler = updates_handler,
  .updates_arg = NULL,

  .notify_need_work = wakeup 
};

char *logname;


int bot_mode;
int verbosity;
int msg_num_mode;
int log_level;

char *config_filename;
char *profile;
char *work_directory;
char *lua_file;
char *python_file;

int disable_colors;
int readline_disabled;
int disable_output;
int reset_authorization;
int port;
int use_ids;
int ipv6_enabled;
char *start_command;
int disable_link_preview;
int enable_json;
int alert_sound;
int exit_code;
int permanent_msg_id_mode;
int permanent_peer_id_mode;
char *home_directory;

void *TLS;

/* {{{ TERMINAL */
static struct termios term_in, term_out;
static int term_set_in;
static int term_set_out;

void get_terminal_attributes (void) {
  if (tcgetattr (STDIN_FILENO, &term_in) < 0) {
  } else {
    term_set_in = 1;
  }
  if (tcgetattr (STDOUT_FILENO, &term_out) < 0) {
  } else {
    term_set_out = 1;
  }
}

void set_terminal_attributes (void) {
  if (term_set_in) {
    if (tcsetattr (STDIN_FILENO, 0, &term_in) < 0) {
      perror ("tcsetattr()");
    }
  }
  if (term_set_out) {
    if (tcsetattr (STDOUT_FILENO, 0, &term_out) < 0) {
      perror ("tcsetattr()");
    }
  }
}
/* }}} */


int str_empty (char *str) {
  return ((str == NULL) || (strlen(str) < 1));
}

char *get_home_directory (void) {
  if (home_directory) { return home_directory; }
  home_directory = getenv("TELEGRAM_HOME");
  if (!str_empty (home_directory)) { return home_directory = strdup (home_directory); }
  home_directory = getenv("HOME");
  if (!str_empty (home_directory)) { return home_directory = strdup (home_directory); }
  struct passwd *current_passwd;
  uid_t user_id;
  setpwent ();
  user_id = getuid ();
  while ((current_passwd = getpwent ())) {
    if (current_passwd->pw_uid == user_id) {
      home_directory = strdup (current_passwd->pw_dir);
      break;
    }
  }
  endpwent ();
  if (str_empty (home_directory)) { home_directory = strdup ("."); }
  return home_directory;
}

char *make_path (const char *a, const char *b) {
  size_t la = strlen (a);
  size_t lb = strlen (b);
  char *res = malloc (la + lb + 2);
  assert (res);
  memcpy (res, a, la);
  res[la] = '/';
  res[la + lb + 1] = 0;
  memcpy (res + la + 1, b, lb);
  return res;
}

char *get_config_directory (void) {
  char *config_directory;
  config_directory = getenv("TELEGRAM_CONFIG_DIR");
  if (!str_empty (config_directory)) { return strdup (config_directory); }
  // XDG: http://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html
  config_directory = getenv("XDG_CONFIG_HOME");
  if (!str_empty (config_directory)) {
    return make_path (config_directory, CONFIG_DIRECTORY);
  }
  return make_path (get_home_directory (), CONFIG_DIRECTORY);
}

char *get_config_filename (void) {
  return config_filename;
}

char *make_full_path (char *s) {
  assert (s);
  if (*s != '/') {
    char *t = make_path (get_home_directory (), s);
    free (s);
    s = t;
  }
  return s;
}

void running_for_first_time (void) {
  if (!str_empty (config_filename)) {
    return; // Do not create custom config file
  }
  char *config_directory = get_config_directory ();

  config_filename = make_path (config_directory, CONFIG_FILE);
  config_filename = make_full_path (config_filename);

  if (!disable_output) {
    printf ("I: config dir=[%s]\n", config_directory);
    printf ("I: config file=[%s]\n", config_filename);
  }

  int config_file_fd;
  
  if (!mkdir (config_directory, CONFIG_DIRECTORY_MODE)) {
    if (!disable_output) {
      printf ("[%s] created\n", config_directory);
    }
  }

  free (config_directory);
  config_directory = NULL;
  // see if config file is there
  if (access (config_filename, R_OK) != 0) {
    // config file missing, so touch it
    config_file_fd = open (config_filename, O_CREAT | O_RDWR, 0600);
    if (config_file_fd == -1)  {
      perror ("open[config_file]");
      printf ("I: config_file=[%s]\n", config_filename);
      exit (EXIT_FAILURE);
    }
    if (write (config_file_fd, DEFAULT_CONFIG_CONTENTS, strlen (DEFAULT_CONFIG_CONTENTS)) <= 0) {
      perror ("write[config_file]");
      exit (EXIT_FAILURE);
    }
    close (config_file_fd);
  }
}

#ifdef HAVE_LIBCONFIG
void parse_config_val (config_t *conf, char **s, char *param_name, const char *default_name, const char *path) {
  static char buf[1000]; 
  size_t l = 0;
  if (profile) {
    l = strlen (profile);
    memcpy (buf, profile, l);
    buf[l ++] = '.';
  }
  *s = 0;
  const char *r = NULL;
  strcpy (buf + l, param_name);
  config_lookup_string (conf, buf, &r);
  if (r) {
    if (path && *r != '/') {
      *s = make_path (path, r);
    } else {
      *s = strdup (r);
    }
  } else {
    if (path && default_name) {
      *s = make_path (path, default_name);
    } else {
      *s  = default_name ? strdup (default_name) : NULL;
    }
  }
}

void parse_config (void) {
  //config_filename = make_full_path (config_filename);
  
  config_t conf;
  config_init (&conf);
  if (config_read_file (&conf, config_filename) != CONFIG_TRUE) {
    fprintf (stderr, "Can not read config '%s': error '%s' on the line %d\n", config_filename, config_error_text (&conf), config_error_line (&conf));
    exit (2);
  }

  if (!profile) {
    config_lookup_string (&conf, "default_profile", (void *)&profile);
  }
  
  if (!disable_output) {
    printf ("I: profile = '%s'\n", profile);
  }

  static char buf[1000];
  size_t l = 0;
  if (profile) {
    l = strlen (profile);
    memcpy (buf, profile, l);
    buf[l ++] = '.';
  }
  
  int test_mode = 0;
  strcpy (buf + l, "test");
  config_lookup_bool (&conf, buf, &test_mode);
  if (test_mode) {
    params.is_test_dc = 1;
  }
  
  strcpy (buf + l, "log_level");
  long long t = log_level;
  config_lookup_int (&conf, buf, (void *)&t);
  log_level = (int)t;
  
  if (!msg_num_mode) {
    strcpy (buf + l, "msg_num");
    config_lookup_bool (&conf, buf, &msg_num_mode);
  }
  
  char *config_directory;

  parse_config_val (&conf, &config_directory, "config_directory", profile ? profile : "", CONFIG_DIRECTORY);
  config_directory = make_full_path (config_directory);
  
  if (!disable_output) {
    printf ("I: config_directory = '%s'\n", config_directory);
  }

  char *data_directory = NULL;
  parse_config_val (&conf, &data_directory, "data_directory", "data", config_directory);
  if (!disable_output) {
    printf ("I: data_directory = '%s'\n", data_directory);
  }
  params.database_directory = strdup (data_directory);
  params.files_directory = strdup (data_directory);

  if (!logname) {
    parse_config_val (&conf, &logname, "logname", 0, config_directory);
    printf ("I: logname = '%s'\n", logname);
    if (logname) {
      int fd = open (logname, O_WRONLY | O_APPEND | O_CREAT);
      if (fd < 0) {
        logprintf ("Can not open logfile '%s': %m\n", logname);
      }
      assert (dup2 (fd, 2) == 2);
      close (fd);
    }
  }

  if (!verbosity) {
    strcpy (buf + l, "verbosity");
    config_lookup_int (&conf, buf, &verbosity);
    TdCClientSetVerbosity (verbosity);
  }

  if (!lua_file) {
    parse_config_val (&conf, &lua_file, "lua_script", 0, config_directory);
    printf ("I: lua_file = '%s'\n", lua_file);
  }
  
  if (!python_file) {
    parse_config_val (&conf, &python_file, "python_script", 0, config_directory);
  }
  
  if (!mkdir (config_directory, CONFIG_DIRECTORY_MODE)) {
    if (!disable_output) {
      printf ("[%s] created\n", config_directory);
    }
  }
  if (!mkdir (data_directory, CONFIG_DIRECTORY_MODE)) {
    if (!disable_output) {
      printf ("[%s] created\n", data_directory);
    }
  }
  free (data_directory);
  free (config_directory);
  config_directory = NULL;
  config_destroy (&conf);
}
#else
void parse_config (void) {
  if (!disable_output) {
    printf ("libconfig not enabled\n");
  }

  char *config_directory = make_path (get_home_directory (), CONFIG_DIRECTORY);
  char *data_directory = make_path (config_directory, "data");
  
  params.database_directory = strdup (data_directory);
  params.files_directory = strdup (data_directory);
  
  if (!mkdir (config_directory, CONFIG_DIRECTORY_MODE)) {
    if (!disable_output) {
      printf ("[%s] created\n", config_directory);
    }
  }
  if (!mkdir (data_directory, CONFIG_DIRECTORY_MODE)) {
    if (!disable_output) {
      printf ("[%s] created\n", data_directory);
    }
  }
  free (data_directory);
  free (config_directory);
  config_directory = NULL;
}
#endif

void inner_main (void) {
  loop ();
}

void usage (void) {
  printf ("%s Usage\n", PROGNAME);
    
  printf ("  --phone/-u                           specify username (would not be asked during authorization)\n");
  printf ("  --verbosity/-v                       increase verbosity (0-ERROR 1-WARNIN 2-NOTICE 3+-DEBUG-levels)\n");
  printf ("  --enable-msg-id/-N                   message num mode\n");
  #ifdef HAVE_LIBCONFIG
  printf ("  --config/-c                          config file name\n");
  printf ("  --profile/-p                         use specified profile\n");
  #endif
  printf ("  --wait-dialog-list/-W                send dialog_list query and wait for answer before reading input\n");
  printf ("  --disable-colors/-C                  disable color output\n");
  printf ("  --disable-readline/-R                disable readline\n");
  printf ("  --alert/-A                           enable bell notifications\n");
  printf ("  --daemonize/-d                       daemon mode\n");
  printf ("  --logname/-L <log-name>              log file name\n");
  printf ("  --username/-U <user-name>            change uid after start\n");
  printf ("  --groupname/-G <group-name>          change gid after start\n");
  printf ("  --disable-output/-D                  disable output\n");
  printf ("  --tcp-port/-P <port>                 port to listen for input commands\n");
  printf ("  --udp-socket/-S <socket-name>        unix socket to create\n");
  printf ("  --exec/-e <commands>                 make commands end exit\n");
  printf ("  --disable-names/-I                   use user and chat IDs in updates instead of names\n");
  printf ("  --help/-h                            prints this help\n");
  printf ("  --accept-any-tcp                     accepts tcp connections from any src (only loopback by default)\n");
  printf ("  --disable-link-preview               disables server-side previews to links\n");
  printf ("  --bot/-b                             bot mode\n");  
  #ifdef USE_JSON
  printf ("  --json                               prints answers and values in json format\n");
  #endif
  #ifdef USE_PYTHON
  printf ("  --python-script/-Z <script-name>     python script file\n");
  #endif
  printf ("  --permanent-msg-ids                  use permanent msg ids\n");
  printf ("  --permanent-peer-ids                 use permanent peer ids\n");

  exit (1);
}

//extern char *rsa_public_key_name;
//extern int default_dc_num;



int register_mode;
int disable_auto_accept;
int wait_dialog_list;

int daemonize=0;


void reopen_logs (void) {
  int fd;
  fflush (stdout);
  fflush (stderr);
  if ((fd = open ("/dev/null", O_RDWR, 0)) != -1) {
    dup2 (fd, 0);
    dup2 (fd, 1);
    dup2 (fd, 2);
    if (fd > 2) {
      close (fd);
    }
  }
  if (logname && (fd = open (logname, O_WRONLY|O_APPEND|O_CREAT, 0640)) != -1) {
    dup2 (fd, 1);
    dup2 (fd, 2);
    if (fd > 2) {
      close (fd);
    }
  }
}


static void sigusr1_handler (const int sig) {
  fprintf(stderr, "got SIGUSR1, rotate logs.\n");
  reopen_logs ();
  signal (SIGUSR1, sigusr1_handler);
}

static void sighup_handler (const int sig) {
  fprintf(stderr, "got SIGHUP.\n");
  signal (SIGHUP, sighup_handler);
}

char *set_user_name;
char *set_group_name;
int accept_any_tcp;
char *bot_hash;

int change_user_group () {
  char *username = set_user_name;
  char *groupname = set_group_name;
  struct passwd *pw;
  /* lose root privileges if we have them */
  if (getuid() == 0 || geteuid() == 0) {
    if (username == 0 || *username == '\0') {
      username = "telegramd";
    }
    if ((pw = getpwnam (username)) == 0) {
      fprintf (stderr, "change_user_group: can't find the user %s to switch to\n", username);
      return -1;
    }
    gid_t gid = pw->pw_gid;
    if (setgroups (1, &gid) < 0) {
      fprintf (stderr, "change_user_group: failed to clear supplementary groups list: %m\n");
      return -1;
    }

    if (groupname) {
      struct group *g = getgrnam (groupname);
      if (g == NULL) {
        fprintf (stderr, "change_user_group: can't find the group %s to switch to\n", groupname);
        return -1;
      }
      gid = g->gr_gid;
    }

    if (setgid (gid) < 0) {
      fprintf (stderr, "change_user_group: setgid (%d) failed. %m\n", (int) gid);
      return -1;
    }

    if (setuid (pw->pw_uid) < 0) {
      fprintf (stderr, "change_user_group: failed to assume identity of user %s\n", username);
      return -1;
    } else {
      pw = getpwuid(getuid());
      setenv("USER", pw->pw_name, 1);
      setenv("HOME", pw->pw_dir, 1);
      setenv("SHELL", pw->pw_shell, 1);
    }
  }
  return 0;
}

char *unix_socket;

void args_parse (int argc, char **argv) {
  static struct option long_options[] = {
    {"verbosity", no_argument, 0, 'v'},
    {"enable-msg-id", no_argument, 0, 'N'},
#ifdef HAVE_LIBCONFIG
    {"config", required_argument, 0, 'c'},
    {"profile", required_argument, 0, 'p'},
#endif
    {"log-level", required_argument, 0, 'l'},
#ifdef USE_LUA
    {"lua-script", required_argument, 0, 's'},
#endif
    {"wait-dialog-list", no_argument, 0, 'W'},
    {"disable-colors", no_argument, 0, 'C'},
    {"disable-readline", no_argument, 0, 'R'},
    {"alert", no_argument, 0, 'A'},
    {"daemonize", no_argument, 0, 'd'},
    {"logname", required_argument, 0, 'L'},
    {"username", required_argument, 0, 'U'},
    {"groupname", required_argument, 0, 'G'},
    {"disable-output", no_argument, 0, 'D'},
    {"tcp-port", required_argument, 0, 'P'},
    {"unix-socket", required_argument, 0, 'S'},
    {"exec", required_argument, 0, 'e'},
    {"disable-names", no_argument, 0, 'I'},
    {"bot", optional_argument, 0, 'b'},
    {"help", no_argument, 0, 'h'},
    {"accept-any-tcp", no_argument, 0,  1001},
    {"disable-link-preview", no_argument, 0, 1002},
    {"json", no_argument, 0, 1003},
    {"python-script", required_argument, 0, 'Z'},
    {"permanent-msg-ids", no_argument, 0, 1004},
    {"permanent-peer-ids", no_argument, 0, 1005},
    {0,         0,                 0,  0 }
  };



  int opt = 0;
  while ((opt = getopt_long (argc, argv, "u:hk:vNl:fEwWCRAdL:DU:G:qP:S:e:I6b"
#ifdef HAVE_LIBCONFIG
  "c:p:"
#endif
#ifdef USE_LUA
  "s:"
#endif
#ifdef USE_PYTHON
  "Z:"
#endif
  , long_options, NULL
  
  )) != -1) {
    switch (opt) {
    case 'b':
      bot_mode ++;
      if (optarg) {
        bot_hash = optarg;
      }
      break;
    case 1001:
      accept_any_tcp = 1;
      break;
    case 'v':
      verbosity ++;
      break;
    case 'N':
      msg_num_mode ++;
      break;
#ifdef HAVE_LIBCONFIG
    case 'c':
      config_filename = optarg;
      break;
    case 'p':
      profile = optarg;
      assert (strlen (profile) <= 100);
      break;
#endif
    case 'l':
      log_level = atoi (optarg);
      break;
    case 'E':
      disable_auto_accept = 1;
      break;
#ifdef USE_LUA
    case 's':
      lua_file = strdup (optarg);
      break;
#endif
    case 'W':
      wait_dialog_list = 1;
      break;
#ifdef USE_PYTHON
    case 'Z':
      python_file = strdup (optarg);
      break;
#endif
    case 'C':
      disable_colors ++;
      break;
    case 'R':
      readline_disabled ++;
      break;
    case 'A':
      alert_sound = 1;
      break;
    case 'd':
      daemonize ++;
      break;
    case 'L':
      logname = optarg;
      break;
    case 'U':
      set_user_name = optarg;
      break;
    case 'G':
      set_group_name = optarg;
      break;
    case 'D':
      disable_output ++;
      break;
    case 'q':
      reset_authorization ++;
      break;
    case 'P':
      port = atoi (optarg);
      break;
    case 'S':
      unix_socket = optarg;
      break;
    case 'e':
      start_command = optarg;
      break;
    case 'I':
      use_ids ++;
      break;
    case 1002:
      disable_link_preview = 2;
      break;
    case 1003:
      enable_json = 1;
      break;
    case 1004:
      permanent_msg_id_mode = 1;
      break;
    case 1005:
      permanent_peer_id_mode = 1;
      break;
    case 'h':
    default:
      usage ();
      break;
    }
  }
}

#ifdef HAVE_EXECINFO_H
void print_backtrace (void) {
  void *buffer[255];
  const int calls = backtrace (buffer, sizeof (buffer) / sizeof (void *));
  backtrace_symbols_fd (buffer, calls, 1);
}
#else
void print_backtrace (void) {
  if (write (1, "No libexec. Backtrace disabled\n", 32) < 0) {
    // Sad thing
  }
}
#endif

int sfd;
int usfd;

void termination_signal_handler (int signum) {
  if (!readline_disabled) {
    rl_free_line_state ();
    rl_cleanup_after_signal ();
  }
  
  if (write (1, "SIGNAL received\n", 18) < 0) { 
    // Sad thing
  }
 
  if (unix_socket) {
    unlink (unix_socket);
  }
  
  if (usfd > 0) {
    close (usfd);
  }
  if (sfd > 0) {
    close (sfd);
  }
  print_backtrace ();
  
  exit (EXIT_FAILURE);
}

volatile int sigterm_cnt;

void sig_term_handler (int signum __attribute__ ((unused))) {
  signal (signum, termination_signal_handler);
  //set_terminal_attributes ();
  if (write (1, "SIGTERM/SIGINT received\n", 25) < 0) { 
    // Sad thing
  }
  sigterm_cnt ++;
}

void do_halt (int error) {
  int retval;
  if (daemonize) {
    return;
  }

  if (!readline_disabled) {
    rl_free_line_state ();
    rl_cleanup_after_signal ();
  }

  if (write (1, "halt\n", 5) < 0) { 
    // Sad thing
  }
 
  if (unix_socket) {
    unlink (unix_socket);
  }
  
  if (usfd > 0) {
    close (usfd);
  }
  if (sfd > 0) {
    close (sfd);
  }
 
  if (exit_code) {
    retval = exit_code;
  } else {
    retval = error ? EXIT_FAILURE : EXIT_SUCCESS;
  }

  exit (retval);
}

int main (int argc, char **argv) {
  signal (SIGSEGV, termination_signal_handler);
  signal (SIGABRT, termination_signal_handler);
  signal (SIGBUS, termination_signal_handler);
  signal (SIGQUIT, termination_signal_handler);
  signal (SIGFPE, termination_signal_handler);
  
  signal (SIGTERM, sig_term_handler);
  signal (SIGINT, sig_term_handler);

  #ifndef READLINE_GNU
  setlocale (LC_ALL, "en_US.UTF-8");
  #endif

  //tdlib_set_logger_function (logprintf);
  //tdlib_do_start_scheduler ();
  rl_catch_signals = 0;


  log_level = 3;
  
  args_parse (argc, argv);
  
  TdCClientSetVerbosity (verbosity);
  
  change_user_group ();

  if (port > 0) {
    struct sockaddr_in serv_addr;
    int yes = 1;
    sfd = socket (AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
      perror ("socket");
      exit(1);
    }

    if(setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0) {
      perror("setsockopt");
      exit(1);
    }
    memset (&serv_addr, 0, sizeof (serv_addr));
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = accept_any_tcp ? INADDR_ANY : htonl (0x7f000001);
    serv_addr.sin_port = htons ((short)port);
 
    if (bind (sfd, (struct sockaddr *) &serv_addr, sizeof (serv_addr)) < 0) {
      perror ("bind");
      exit(1);
    }

    listen (sfd, 5);
  } else {
    sfd = -1;
  }
  
  if (unix_socket) {
    assert (strlen (unix_socket) < 100);
    struct sockaddr_un serv_addr;

    usfd = socket (AF_UNIX, SOCK_STREAM, 0);
    if (usfd < 0) {
      perror ("socket");
      exit(1);
    }

    memset (&serv_addr, 0, sizeof (serv_addr));
    
    serv_addr.sun_family = AF_UNIX;

    snprintf (serv_addr.sun_path, sizeof(serv_addr.sun_path), "%s", unix_socket);
 
    if (bind (usfd, (struct sockaddr *) &serv_addr, sizeof (serv_addr)) < 0) {
      perror ("bind");
      exit(1);
    }

    listen (usfd, 5);    
  } else {
    usfd = -1;
  }

  if (logname) {
    int fd = open (logname, O_WRONLY | O_APPEND | O_CREAT);
    if (fd < 0) {
      logprintf ("Can not open logfile '%s': %m\n", logname);
    }
    assert (dup2 (fd, 2) == 2);
    close (fd);
  }

  if (daemonize) {
    signal (SIGHUP, sighup_handler);
    reopen_logs ();
  }
  signal (SIGUSR1, sigusr1_handler);

  if (!disable_output) {
    printf (
      "Telegram-cli version " TELEGRAM_CLI_VERSION ", Copyright (C) 2013-2015 Vitaly Valtman\n"
      "Telegram-cli comes with ABSOLUTELY NO WARRANTY; for details type `show_license'.\n"
      "This is free software, and you are welcome to redistribute it\n"
      "under certain conditions; type `show_license' for details.\n"
      "Telegram-cli uses tdlib (commit " GIT_COMMIT ")\n"
      "Telegram-cli includes software developed by the OpenSSL Project\n"
      "for use in the OpenSSL Toolkit. (http://www.openssl.org/)\n"
#ifdef READLINE_GNU
      "Telegram-cli uses libreadline\n"
#else
      "Telegram-cli uses libedit\n"
#endif
    );
  }
  running_for_first_time ();
  parse_config ();

  get_terminal_attributes ();

  #ifdef USE_LUA
  if (lua_file) {
    lua_init (lua_file);
  }
  #endif
  #ifdef USE_PYTHON
  if (python_file) {
    py_init (python_file);
  }
  #endif


  inner_main ();
  
  return 0;
}
