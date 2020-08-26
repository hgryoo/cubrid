/*
 * Copyright (C) 2008 Search Solution Corporation
 * Copyright (C) 2016 CUBRID Corporation
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * jsp_sr.c - Java Stored Procedure Server Module Source
 */

#ident "$Id$"

#include "jsp_sr.h"

#include "config.h"

#if defined(WINDOWS)
#include <windows.h>
#define DELAYIMP_INSECURE_WRITABLE_HOOKS
#include <Delayimp.h>
#pragma comment(lib, "delayimp")
#pragma comment(lib, "jvm")
#else /* WINDOWS */
#include <dlfcn.h>
#endif /* !WINDOWS */

#include <jni.h>
#include <locale.h>
#include <assert.h>
#include <vector>
#include <string>
#include <sstream>
#include <iterator>

#include "environment_variable.h"
#include "system_parameter.h"
#include "release_string.h"
#include "memory_alloc.h"
#include "error_manager.h"

#include "dbtype.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "numeric_opfunc.h"
#include "unicode_support.h"
#include "db_date.h"
#include "set_object.h"
#include "language_support.h"

#if !defined(WINDOWS)
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#else /* not WINDOWS */
#include <winsock2.h>
#include <windows.h>
#endif /* not WINDOWS */


typedef enum
{
  SP_TYPE_PROCEDURE = 1,
  SP_TYPE_FUNCTION
} SP_TYPE_ENUM;

typedef enum
{
  SP_MODE_IN = 1,
  SP_MODE_OUT,
  SP_MODE_INOUT
} SP_MODE_ENUM;

typedef enum
{
  SP_LANG_JAVA = 1
} SP_LANG_ENUM;

typedef enum
{
  SP_CODE_INVOKE = 0x01,
  SP_CODE_RESULT = 0x02,
  SP_CODE_ERROR = 0x04,
  SP_CODE_INTERNAL_JDBC = 0x08,
  SP_CODE_DESTROY = 0x10
} SP_CODE;

#if defined(sparc)
#define JVM_LIB_PATH "jre/lib/sparc/client"
#elif defined(WINDOWS)
#if __WORDSIZE == 32
#define JVM_LIB_PATH_JDK "jre\\bin\\client"
#define JVM_LIB_PATH_JRE "bin\\client"
#else
#define JVM_LIB_PATH_JDK "jre\\bin\\server"
#define JVM_LIB_PATH_JRE "bin\\server"
#endif
#elif defined(HPUX) && defined(IA64)
#define JVM_LIB_PATH "jre/lib/IA64N/hotspot"
#elif defined(HPUX) && !defined(IA64)
#define JVM_LIB_PATH "jre/lib/PA_RISC2.0/hotspot"
#elif defined(AIX)
#if __WORDSIZE == 32
#define JVM_LIB_PATH "jre/bin/classic"
#elif __WORDSIZE == 64
#define JVM_LIB_PATH "jre/lib/ppc64/classic"
#endif
#elif defined(__i386) || defined(__x86_64)
#if __WORDSIZE == 32
#define JVM_LIB_PATH "jre/lib/i386/client"
#else
#define JVM_LIB_PATH "jre/lib/amd64/server"
#endif
#else /* ETC */
#define JVM_LIB_PATH ""
#endif /* ETC */

#if !defined(WINDOWS)
#if defined(AIX)
#define JVM_LIB_FILE "libjvm.so"
#elif defined(HPUX) && !defined(IA64)
#define JVM_LIB_FILE "libjvm.sl"
#else /* not AIX , not ( HPUX && (not IA64)) */
#define JVM_LIB_FILE "libjvm.so"
#endif /* not AIX , not ( HPUX && (not IA64)) */
#endif /* !WINDOWS */

#if defined(WINDOWS)
#define REGKEY_JAVA     "Software\\JavaSoft\\Java Runtime Environment"
#endif /* WINDOWS */

#define BUF_SIZE        2048
typedef jint (*CREATE_VM_FUNC) (JavaVM **, void **, void *);

#ifdef __cplusplus
#define JVM_FindClass(ENV, NAME)	\
	(ENV)->FindClass(NAME)
#define JVM_GetStaticMethodID(ENV, CLAZZ, NAME, SIG)	\
	(ENV)->GetStaticMethodID(CLAZZ, NAME, SIG)
#define JVM_NewStringUTF(ENV, BYTES)	\
	(ENV)->NewStringUTF(BYTES);
#define JVM_NewObjectArray(ENV, LENGTH, ELEMENTCLASS, INITIALCLASS)	\
	(ENV)->NewObjectArray(LENGTH, ELEMENTCLASS, INITIALCLASS)
#define JVM_SetObjectArrayElement(ENV, ARRAY, INDEX, VALUE)	\
	(ENV)->SetObjectArrayElement(ARRAY, INDEX, VALUE)
#define JVM_CallStaticIntMethod(ENV, CLAZZ, METHODID, ARGS)	\
	(ENV)->CallStaticIntMethod(CLAZZ, METHODID, ARGS)
#else
#define JVM_FindClass(ENV, NAME)	\
	(*ENV)->FindClass(ENV, NAME)
#define JVM_GetStaticMethodID(ENV, CLAZZ, NAME, SIG)	\
	(*ENV)->GetStaticMethodID(ENV, CLAZZ, NAME, SIG)
#define JVM_NewStringUTF(ENV, BYTES)	\
	(*ENV)->NewStringUTF(ENV, BYTES);
#define JVM_NewObjectArray(ENV, LENGTH, ELEMENTCLASS, INITIALCLASS)	\
	(*ENV)->NewObjectArray(ENV, LENGTH, ELEMENTCLASS, INITIALCLASS)
#define JVM_SetObjectArrayElement(ENV, ARRAY, INDEX, VALUE)	\
	(*ENV)->SetObjectArrayElement(ENV, ARRAY, INDEX, VALUE)
#define JVM_CallStaticIntMethod(ENV, CLAZZ, METHODID, ARGS)	\
	(*ENV)->CallStaticIntMethod(ENV, CLAZZ, METHODID, ARGS)
#endif

JavaVM *jvm = NULL;
jint sp_port = -1;
volatile SOCKET sock_fd = INVALID_SOCKET;

#if defined(WINDOWS)
int get_java_root_path (char *path);
FARPROC WINAPI delay_load_hook (unsigned dliNotify, PDelayLoadInfo pdli);
LONG WINAPI delay_load_dll_exception_filter (PEXCEPTION_POINTERS pep);

extern PfnDliHook __pfnDliNotifyHook2 = delay_load_hook;
extern PfnDliHook __pfnDliFailureHook2 = delay_load_hook;

#else /* WINDOWS */
static void *jsp_get_create_java_vm_function_ptr (void);
#endif /* !WINDOWS */

#if defined(WINDOWS)

/*
 * get_java_root_path()
 *   return: return FALSE on error othrewise true
 *   path(in/out): get java root path
 *
 * Note:
 */

int
get_java_root_path (char *path)
{
  DWORD rc;
  DWORD len;
  DWORD dwType;
  char currentVersion[16];
  char regkey_java_current_version[BUF_SIZE];
  char java_root_path[BUF_SIZE];
  HKEY hKeyReg;

  if (!path)
    {
      return false;
    }

  rc = RegOpenKeyEx (HKEY_LOCAL_MACHINE, REGKEY_JAVA, 0, KEY_QUERY_VALUE, &hKeyReg);
  if (rc != ERROR_SUCCESS)
    {
      return false;
    }

  len = sizeof (currentVersion);
  rc = RegQueryValueEx (hKeyReg, "CurrentVersion", 0, &dwType, (LPBYTE) currentVersion, &len);

  if (hKeyReg)
    {
      RegCloseKey (hKeyReg);
    }

  if (rc != ERROR_SUCCESS)
    {
      return false;
    }

  hKeyReg = NULL;
  sprintf (regkey_java_current_version, "%s\\%s", REGKEY_JAVA, currentVersion);
  rc = RegOpenKeyEx (HKEY_LOCAL_MACHINE, regkey_java_current_version, 0, KEY_QUERY_VALUE, &hKeyReg);

  if (rc != ERROR_SUCCESS)
    {
      return false;
    }

  len = sizeof (java_root_path);
  rc = RegQueryValueEx (hKeyReg, "JavaHome", 0, &dwType, (LPBYTE) java_root_path, &len);

  if (hKeyReg)
    {
      RegCloseKey (hKeyReg);
    }

  if (rc != ERROR_SUCCESS)
    {
      return false;
    }

  strcpy (path, java_root_path);
  return true;
}

/*
 * delay_load_hook -
 *   return:
 *   dliNotify(in):
 *   pdli(in):
 *
 * Note:
 */

FARPROC WINAPI
delay_load_hook (unsigned dliNotify, PDelayLoadInfo pdli)
{
  FARPROC fp = NULL;

  switch (dliNotify)
    {
    case dliFailLoadLib:
      {
	char *java_home = NULL, *tmp = NULL, *tail;
	char jvm_lib_path[BUF_SIZE];
	void *libVM;

	java_home = getenv ("JAVA_HOME");
	tail = JVM_LIB_PATH_JDK;
	if (java_home == NULL)
	  {
	    tmp = (char *) malloc (BUF_SIZE);
	    if (tmp)
	      {
		if (get_java_root_path (tmp))
		  {
		    java_home = tmp;
		    tail = JVM_LIB_PATH_JRE;
		  }
	      }
	  }

	if (java_home)
	  {
	    sprintf (jvm_lib_path, "%s\\%s\\jvm.dll", java_home, tail);
	    libVM = LoadLibrary (jvm_lib_path);

	    if (libVM)
	      {
		fp = (FARPROC) (HMODULE) libVM;
	      }
	  }

	if (tmp)
	  {
	    free_and_init (tmp);
	  }
      }
      break;

    default:
      break;
    }

  return fp;
}

/*
 * delay_load_dll_exception_filter -
 *   return:
 *   pep(in):
 *
 * Note:
 */

LONG WINAPI
delay_load_dll_exception_filter (PEXCEPTION_POINTERS pep)
{
  switch (pep->ExceptionRecord->ExceptionCode)
    {
    case VcppException (ERROR_SEVERITY_ERROR, ERROR_MOD_NOT_FOUND):
    case VcppException (ERROR_SEVERITY_ERROR, ERROR_PROC_NOT_FOUND):
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_JVM_LIB_NOT_FOUND, 1, "jvm.dll");
      break;

    default:
      break;
    }

  return EXCEPTION_EXECUTE_HANDLER;
}

#else /* WINDOWS */

/*
 * jsp_get_create_java_vm_func_ptr
 *   return: return java vm function pointer
 *
 * Note:
 */

static void *
jsp_get_create_java_vm_function_ptr (void)
{
  char *java_home = NULL;
  char jvm_library_path[PATH_MAX];
  void *libVM_p;

  libVM_p = dlopen (JVM_LIB_FILE, RTLD_LAZY | RTLD_GLOBAL);
  if (libVM_p == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_SP_JVM_LIB_NOT_FOUND, 1, dlerror ());

      java_home = getenv ("JAVA_HOME");
      if (java_home != NULL)
	{
	  snprintf (jvm_library_path, PATH_MAX - 1, "%s/%s/%s", java_home, JVM_LIB_PATH, JVM_LIB_FILE);
	  libVM_p = dlopen (jvm_library_path, RTLD_LAZY | RTLD_GLOBAL);

	  if (libVM_p == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_JVM_LIB_NOT_FOUND, 1, dlerror ());
	      return NULL;
	    }
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_JVM_LIB_NOT_FOUND, 1, dlerror ());
	  return NULL;
	}
    }

  return dlsym (libVM_p, "JNI_CreateJavaVM");
}

#endif /* !WINDOWS */


/*
 * jsp_create_java_vm
 *   return: create java vm
 *
 * Note:
 */
static int
jsp_create_java_vm (JNIEnv ** env_p, JavaVMInitArgs * vm_arguments)
{
  int res;
#if defined(WINDOWS)
  __try
  {
    res = JNI_CreateJavaVM (&jvm, (void **) env_p, vm_arguments);
  }
  __except (delay_load_dll_exception_filter (GetExceptionInformation ()))
  {
    res = -1;
  }
#else /* WINDOWS */
  CREATE_VM_FUNC create_vm_func = (CREATE_VM_FUNC) jsp_get_create_java_vm_function_ptr ();
  if (create_vm_func)
    {
      res = (*create_vm_func) (&jvm, (void **) env_p, vm_arguments);
    }
  else
    {
      res = -1;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_JVM_LIB_NOT_FOUND, 1, dlerror ());
    }
#endif /* WINDOWS */
  return res;
}

/*
 * jsp_tokenize_jvm_options
 *  return: tokenized array of string
 *
 */

// *INDENT-OFF*
static std::vector <std::string>
jsp_tokenize_jvm_options (char *opt_str)
{
  std::string str (opt_str);
  std::istringstream iss (str);
  std::vector <std::string> options;
  std::copy (std::istream_iterator <std::string> (iss),
	     std::istream_iterator <std::string> (), std::back_inserter (options));
  return options;
}
// *INDENT-ON*

/*
 * jsp_start_server -
 *   return: Error Code
 *   db_name(in): db name
 *   path(in): path
 *
 * Note:
 */

int
jsp_start_server (const char *db_name, const char *path)
{
  JNIEnv *env_p = NULL;
  jint res;
  jclass cls, string_cls;
  jmethodID mid;
  jstring jstr_dbname, jstr_path, jstr_version, jstr_envroot, jstr_port;
  jobjectArray args;
  JavaVMInitArgs vm_arguments;
  JavaVMOption *options;
  int vm_n_options = 3;
  char classpath[PATH_MAX + 32], logging_prop[PATH_MAX + 32], option_debug[70];
  char debug_flag[] = "-Xdebug";
  char debug_jdwp[] = "-agentlib:jdwp=transport=dt_socket,server=y,address=%d,suspend=n";
  char disable_sig_handle[] = "-Xrs";
  const char *envroot;
  char jsp_file_path[PATH_MAX];
  char port[6] = { 0 };
  char *loc_p, *locale;
  char *jvm_opt_sysprm = NULL;
  int debug_port = -1;

  if (!prm_get_bool_value (PRM_ID_JAVA_STORED_PROCEDURE))
    {
      return NO_ERROR;
    }

  if (jvm != NULL)
    {
      return NO_ERROR;		/* already created */
    }

  envroot = envvar_root ();
  if (envroot == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "envvar_root");
      return ER_SP_CANNOT_START_JVM;
    }

  snprintf (classpath, sizeof (classpath) - 1, "-Djava.class.path=%s",
	    envvar_javadir_file (jsp_file_path, PATH_MAX, "jspserver.jar"));

  snprintf (logging_prop, sizeof (logging_prop) - 1, "-Djava.util.logging.config.file=%s",
	    envvar_javadir_file (jsp_file_path, PATH_MAX, "logging.properties"));

  debug_port = prm_get_integer_value (PRM_ID_JAVA_STORED_PROCEDURE_DEBUG);
  if (debug_port != -1)
    {
      vm_n_options += 2;	/* set debug flag and debugging port */
    }

  jvm_opt_sysprm = (char *) prm_get_string_value (PRM_ID_JAVA_STORED_PROCEDURE_JVM_OPTIONS);
  // *INDENT-OFF*
  std::vector <std::string> opts = jsp_tokenize_jvm_options (jvm_opt_sysprm);
  // *INDENT-ON*
  vm_n_options += (int) opts.size ();
  options = new JavaVMOption[vm_n_options];

  int idx = 3;
  options[0].optionString = classpath;
  options[1].optionString = logging_prop;
  options[2].optionString = disable_sig_handle;
  if (debug_port != -1)
    {
      idx += 2;
      snprintf (option_debug, sizeof (option_debug) - 1, debug_jdwp, debug_port);
      options[3].optionString = debug_flag;
      options[4].optionString = option_debug;
    }

  for (auto it = opts.begin (); it != opts.end (); ++it)
    {
      // *INDENT-OFF*
      options[idx++].optionString = const_cast <char*> (it->c_str ());
      // *INDENT-ON*
    }

  vm_arguments.version = JNI_VERSION_1_4;
  vm_arguments.options = options;
  vm_arguments.nOptions = vm_n_options;
  vm_arguments.ignoreUnrecognized = JNI_TRUE;

  locale = NULL;
  loc_p = setlocale (LC_TIME, NULL);
  if (loc_p != NULL)
    {
      locale = strdup (loc_p);
    }

  res = jsp_create_java_vm (&env_p, &vm_arguments);
  // *INDENT-OFF*
  delete[] options;
  // *INDENT-ON*

#if !defined(WINDOWS)
  if (er_has_error ())
    {
      if (locale != NULL)
	{
	  free (locale);
	}
      return er_errid ();
    }
#endif

  setlocale (LC_TIME, locale);
  if (locale != NULL)
    {
      free (locale);
    }

  if (res < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "JNI_CreateJavaVM");
      jvm = NULL;
      return er_errid ();
    }

  cls = JVM_FindClass (env_p, "com/cubrid/jsp/Server");
  if (cls == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "FindClass: " "com/cubrid/jsp/Server");
      goto error;
    }

  mid = JVM_GetStaticMethodID (env_p, cls, "start", "([Ljava/lang/String;)I");
  if (mid == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "GetStaticMethodID");
      goto error;
    }

  jstr_dbname = JVM_NewStringUTF (env_p, db_name);
  if (jstr_dbname == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "NewStringUTF");
      goto error;
    }

  jstr_path = JVM_NewStringUTF (env_p, path);
  if (jstr_path == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "NewStringUTF");
      goto error;
    }

  jstr_version = JVM_NewStringUTF (env_p, rel_build_number ());
  if (jstr_version == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "NewStringUTF");
      goto error;
    }

  jstr_envroot = JVM_NewStringUTF (env_p, envvar_root ());
  if (jstr_envroot == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "NewStringUTF");
      goto error;
    }

  sprintf (port, "%d", prm_get_integer_value (PRM_ID_JAVA_STORED_PROCEDURE_PORT));
  jstr_port = JVM_NewStringUTF (env_p, port);
  if (jstr_port == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "NewStringUTF");
      goto error;
    }

  string_cls = JVM_FindClass (env_p, "java/lang/String");
  if (string_cls == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "FindClass: " "java/lang/String");
      goto error;
    }

  args = JVM_NewObjectArray (env_p, 5, string_cls, NULL);
  if (args == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "NewObjectArray");
      goto error;
    }

  JVM_SetObjectArrayElement (env_p, args, 0, jstr_dbname);
  JVM_SetObjectArrayElement (env_p, args, 1, jstr_path);
  JVM_SetObjectArrayElement (env_p, args, 2, jstr_version);
  JVM_SetObjectArrayElement (env_p, args, 3, jstr_envroot);
  JVM_SetObjectArrayElement (env_p, args, 4, jstr_port);

  sp_port = JVM_CallStaticIntMethod (env_p, cls, mid, args);
  if (sp_port == -1)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, "CallStaticIntMethod");
      goto error;
    }

  return 0;

error:
  jsp_stop_server ();

  assert (er_errid () != NO_ERROR);
  return er_errid ();
}

/*
 * jsp_stop_server
 *   return: 0
 *
 * Note:
 */

int
jsp_stop_server (void)
{
  if (jsp_jvm_is_loaded ())
    {
      jvm = NULL;
    }

  return NO_ERROR;
}

/*
 * jsp_server_port
 *   return: if disable jsp function and return -1
 *              enable jsp function and return jsp server port
 *
 * Note:
 */

int
jsp_server_port (void)
{
  return sp_port;
}

/*
 * jsp_jvm_is_loaded
 *   return: if disable jsp function and return false
 *              enable jsp function and return not false
 *
 * Note:
 */

int
jsp_jvm_is_loaded (void)
{
  return jvm != NULL;
}

/*
 * jsp_pack_argument
 *   return: packing value for send to jsp server
 *   buffer(in/out): contain packng value
 *   value(in): value for packing
 *
 * Note:
 */

char *
jsp_pack_argument (char *buffer, DB_VALUE * value)
{
  int param_type;
  char *ptr;

  ptr = buffer;
  param_type = DB_VALUE_TYPE (value);
  ptr = or_pack_int (ptr, param_type);

  switch (param_type)
    {
    case DB_TYPE_INTEGER:
      ptr = jsp_pack_int_argument (ptr, value);
      break;

    case DB_TYPE_BIGINT:
      ptr = jsp_pack_bigint_argument (ptr, value);
      break;

    case DB_TYPE_SHORT:
      ptr = jsp_pack_short_argument (ptr, value);
      break;

    case DB_TYPE_FLOAT:
      ptr = jsp_pack_float_argument (ptr, value);
      break;

    case DB_TYPE_DOUBLE:
      ptr = jsp_pack_double_argument (ptr, value);
      break;

    case DB_TYPE_NUMERIC:
      ptr = jsp_pack_numeric_argument (ptr, value);
      break;

    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_STRING:
      ptr = jsp_pack_string_argument (ptr, value);
      break;

    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      break;

    case DB_TYPE_DATE:
      ptr = jsp_pack_date_argument (ptr, value);
      break;
      /* describe_data(); */

    case DB_TYPE_TIME:
      ptr = jsp_pack_time_argument (ptr, value);
      break;

    case DB_TYPE_TIMESTAMP:
      ptr = jsp_pack_timestamp_argument (ptr, value);
      break;

    case DB_TYPE_DATETIME:
      ptr = jsp_pack_datetime_argument (ptr, value);
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      ptr = jsp_pack_set_argument (ptr, value);
      break;

    case DB_TYPE_MONETARY:
      ptr = jsp_pack_monetary_argument (ptr, value);
      break;

    case DB_TYPE_OBJECT:
      ptr = jsp_pack_object_argument (ptr, value);
      break;

    case DB_TYPE_NULL:
      ptr = jsp_pack_null_argument (ptr);
      break;
    default:
      break;
    }

  return ptr;
}

/*
 * jsp_pack_int_argument -
 *   return: return packing value
 *   buffer(in/out): buffer
 *   value(in): value of integer type
 *
 * Note:
 */

 char *
jsp_pack_int_argument (char *buffer, DB_VALUE * value)
{
  int v;
  char *ptr;

  ptr = buffer;
  ptr = or_pack_int (ptr, sizeof (int));
  v = db_get_int (value);
  ptr = or_pack_int (ptr, v);

  return ptr;
}

/*
 * jsp_pack_bigint_argument -
 *   return: return packing value
 *   buffer(in/out): buffer
 *   value(in): value of bigint type
 *
 * Note:
 */
 char *
jsp_pack_bigint_argument (char *buffer, DB_VALUE * value)
{
  DB_BIGINT tmp_value;
  char *ptr;

  ptr = or_pack_int (buffer, sizeof (DB_BIGINT));
  tmp_value = db_get_bigint (value);
  OR_PUT_BIGINT (ptr, &tmp_value);

  return ptr + OR_BIGINT_SIZE;
}

/*
 * jsp_pack_short_argument -
 *   return: return packing value
 *   buffer(in): buffer
 *   value(in): value of short type
 *
 * Note:
 */

 char *
jsp_pack_short_argument (char *buffer, DB_VALUE * value)
{
  short v;
  char *ptr;

  ptr = buffer;
  ptr = or_pack_int (ptr, sizeof (int));
  v = db_get_short (value);
  ptr = or_pack_short (ptr, v);

  return ptr;
}

/*
 * jsp_pack_float_argument -
 *   return: return packing value
 *   buffer(in): buffer
 *   value(in): value of float type
 *
 * Note:
 */

 char *
jsp_pack_float_argument (char *buffer, DB_VALUE * value)
{
  float v;
  char *ptr;

  ptr = buffer;
  ptr = or_pack_int (ptr, sizeof (float));
  v = db_get_float (value);
  ptr = or_pack_float (ptr, v);

  return ptr;
}

/*
 * jsp_pack_double_argument -
 *   return: return packing value
 *   buffer(in): buffer
 *   value(in): value of double type
 *
 * Note:
 */

 char *
jsp_pack_double_argument (char *buffer, DB_VALUE * value)
{
  double v;
  char *ptr;
  char pack_value[OR_DOUBLE_SIZE];

  ptr = or_pack_int (buffer, sizeof (double));
  v = db_get_double (value);
  OR_PUT_DOUBLE (pack_value, v);
  memcpy (ptr, pack_value, OR_DOUBLE_SIZE);

  return ptr + OR_DOUBLE_SIZE;
}

/*
 * jsp_pack_numeric_argument -
 *   return: return packing value
 *   buffer(in): buffer
 *   value(in): value of numeric type
 *
 * Note:
 */

 char *
jsp_pack_numeric_argument (char *buffer, DB_VALUE * value)
{
  char str_buf[81];
  char *ptr;

  ptr = buffer;
  numeric_db_value_print (value, str_buf);
  ptr = or_pack_string (ptr, str_buf);

  return ptr;
}

/*
 * jsp_pack_string_argument -
 *   return: return packing value
 *   buffer(in): buffer
 *   value(in): value of string type
 *
 * Note:
 */

 char *
jsp_pack_string_argument (char *buffer, DB_VALUE * value)
{
  const char *v;
  char *ptr, *decomposed = NULL;
  int v_size;
  int decomp_size;
  bool was_decomposed = false;


  ptr = buffer;
  v = db_get_string (value);
  v_size = (v != NULL) ? strlen (v) : 0;

  if (v_size > 0 && db_get_string_codeset (value) == INTL_CODESET_UTF8 && false)
      //&& unicode_string_need_decompose (v, v_size, &decomp_size, lang_get_generic_unicode_norm ()))
    {
      int alloc_size = decomp_size + 1;

      decomposed = (char *) db_private_alloc (NULL, alloc_size);
      if (decomposed != NULL)
	{
	  //unicode_decompose_string (v, v_size, decomposed, &decomp_size, lang_get_generic_unicode_norm ());
	  /* or_pack_string requires null-terminated string */
	  decomposed[decomp_size] = '\0';
	  assert (decomp_size < alloc_size);

	  v = decomposed;
	  v_size = decomp_size;
	  was_decomposed = true;
	}
      else
	{
	  v = NULL;
	}
    }

  ptr = or_pack_string (ptr, v);

  if (was_decomposed)
    {
      db_private_free (NULL, decomposed);
    }

  return ptr;
}

/*
 * jsp_pack_date_argument -
 *   return: return packing value
 *   buffer(in): buffer
 *   value(in): value of date type
 *
 * Note:
 */

 char *
jsp_pack_date_argument (char *buffer, DB_VALUE * value)
{
  int year, month, day;
  DB_DATE *date;
  char *ptr;

  ptr = buffer;
  date = db_get_date (value);
  db_date_decode (date, &month, &day, &year);

  ptr = or_pack_int (ptr, sizeof (int) * 3);
  ptr = or_pack_int (ptr, year);
  ptr = or_pack_int (ptr, month - 1);
  ptr = or_pack_int (ptr, day);

  return ptr;
}

/*
 * jsp_pack_time_argument -
 *   return: return packing value
 *   buffer(in): buffer
 *   value(in): value of time type
 *
 * Note:
 */

 char *
jsp_pack_time_argument (char *buffer, DB_VALUE * value)
{
  int hour, min, sec;
  DB_TIME *time;
  char *ptr;

  ptr = buffer;
  time = db_get_time (value);
  db_time_decode (time, &hour, &min, &sec);

  ptr = or_pack_int (ptr, sizeof (int) * 3);
  ptr = or_pack_int (ptr, hour);
  ptr = or_pack_int (ptr, min);
  ptr = or_pack_int (ptr, sec);

  return ptr;
}

/*
 * jsp_pack_timestamp_argument -
 *   return: return packing value
 *   buffer(in): buffer
 *   value(in): value of timestamp type
 *
 * Note:
 */

 char *
jsp_pack_timestamp_argument (char *buffer, DB_VALUE * value)
{
  DB_TIMESTAMP *timestamp;
  DB_DATE date;
  DB_TIME time;
  int year, mon, day, hour, min, sec;
  char *ptr;

  ptr = buffer;
  timestamp = db_get_timestamp (value);
  (void) db_timestamp_decode_ses (timestamp, &date, &time);
  db_date_decode (&date, &mon, &day, &year);
  db_time_decode (&time, &hour, &min, &sec);

  ptr = or_pack_int (ptr, sizeof (int) * 6);
  ptr = or_pack_int (ptr, year);
  ptr = or_pack_int (ptr, mon - 1);
  ptr = or_pack_int (ptr, day);
  ptr = or_pack_int (ptr, hour);
  ptr = or_pack_int (ptr, min);
  ptr = or_pack_int (ptr, sec);

  return ptr;
}

/*
 * jsp_pack_datetime_argument -
 *   return: return packing value
 *   buffer(in): buffer
 *   value(in): value of datetime type
 *
 * Note:
 */

 char *
jsp_pack_datetime_argument (char *buffer, DB_VALUE * value)
{
  DB_DATETIME *datetime;
  int year, mon, day, hour, min, sec, msec;
  char *ptr;

  ptr = buffer;
  datetime = db_get_datetime (value);
  db_datetime_decode (datetime, &mon, &day, &year, &hour, &min, &sec, &msec);

  ptr = or_pack_int (ptr, sizeof (int) * 7);
  ptr = or_pack_int (ptr, year);
  ptr = or_pack_int (ptr, mon - 1);
  ptr = or_pack_int (ptr, day);
  ptr = or_pack_int (ptr, hour);
  ptr = or_pack_int (ptr, min);
  ptr = or_pack_int (ptr, sec);
  ptr = or_pack_int (ptr, msec);

  return ptr;
}

/*
 * jsp_pack_set_argument -
 *   return: return packing value
 *   buffer(in): buffer
 *   value(in): value of set type
 *
 * Note:
 */

 char *
jsp_pack_set_argument (char *buffer, DB_VALUE * value)
{
  DB_SET *set;
  int ncol, i;
  DB_VALUE v;
  char *ptr;

  ptr = buffer;
  set = db_get_set (value);
  ncol = set_size (set);

  ptr = or_pack_int (ptr, sizeof (int));
  ptr = or_pack_int (ptr, ncol);

  for (i = 0; i < ncol; i++)
    {
      if (set_get_element (set, i, &v) != NO_ERROR)
	{
	  break;
	}

      ptr = jsp_pack_argument (ptr, &v);
      pr_clear_value (&v);
    }

  return ptr;
}

/*
 * jsp_pack_object_argument -
 *   return: return packing value
 *   buffer(in/out): buffer
 *   value(in): value of object type
 *
 * Note:
 */

char *
jsp_pack_object_argument (char *buffer, DB_VALUE * value)
{
  char *ptr;
  OID *oid;
  MOP mop;

  ptr = buffer;
  mop = db_get_object (value);
  if (mop != NULL)
    {
      //oid = WS_OID (mop);
      assert (false);
    }
  else
    {
      oid = (OID *) (&oid_Null_oid);
    }

  ptr = or_pack_int (ptr, sizeof (int) * 3);
  ptr = or_pack_int (ptr, oid->pageid);
  ptr = or_pack_short (ptr, oid->slotid);
  ptr = or_pack_short (ptr, oid->volid);

  return ptr;
}

/*
 * jsp_pack_monetary_argument -
 *   return: return packing value
 *   buffer(in/out): buffer
 *   value(in): value of monetary type
 *
 * Note:
 */

 char *
jsp_pack_monetary_argument (char *buffer, DB_VALUE * value)
{
  DB_MONETARY *v;
  char pack_value[OR_DOUBLE_SIZE];
  char *ptr;

  ptr = or_pack_int (buffer, sizeof (double));
  v = db_get_monetary (value);
  OR_PUT_DOUBLE (pack_value, v->amount);
  memcpy (ptr, pack_value, OR_DOUBLE_SIZE);

  return ptr + OR_DOUBLE_SIZE;
}

/*
 * jsp_pack_null_argument -
 *   return: return null packing value
 *   buffer(in/out): buffer
 *
 * Note:
 */

 char *
jsp_pack_null_argument (char *buffer)
{
  char *ptr;

  ptr = buffer;
  ptr = or_pack_int (ptr, 0);

  return ptr;
}

/*
 * jsp_pack_argument
 *   return: packing value for send to jsp server
 *   buffer(in/out): contain packng value
 *   value(in): value for packing
 *
 * Note:
 */

SOCKET
jsp_connect_server (void)
{
  struct sockaddr_in tcp_srv_addr;
  int success = -1;
  int server_port = -1;
  unsigned int inaddr;
  int b;
  char *server_host = (char *) "127.0.0.1";	/* assume as local host */

  union
  {
    struct sockaddr_in in;
  } saddr_buf;
  struct sockaddr *saddr = (struct sockaddr *) &saddr_buf;
  socklen_t slen;

  server_port = jsp_server_port ();
  if (server_port < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NOT_RUNNING_JVM, 0);
      return INVALID_SOCKET;
    }

  inaddr = inet_addr (server_host);
  memset ((void *) &tcp_srv_addr, 0, sizeof (tcp_srv_addr));
  tcp_srv_addr.sin_family = AF_INET;
  tcp_srv_addr.sin_port = htons (server_port);

  if (inaddr != INADDR_NONE)
    {
      memcpy ((void *) &tcp_srv_addr.sin_addr, (void *) &inaddr, sizeof (inaddr));
    }
  else
    {
      struct hostent *hp;
      hp = gethostbyname (server_host);

      if (hp == NULL)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_TCP_HOST_NAME_ERROR, 1, server_host);
	  return INVALID_SOCKET;
	}
      memcpy ((void *) &tcp_srv_addr.sin_addr, (void *) hp->h_addr, hp->h_length);
    }
  slen = sizeof (tcp_srv_addr);
  memcpy ((void *) saddr, (void *) &tcp_srv_addr, slen);

  sock_fd = socket (saddr->sa_family, SOCK_STREAM, 0);
  if (IS_INVALID_SOCKET (sock_fd))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_CONNECT_JVM, 1, "socket()");
      return INVALID_SOCKET;
    }
  else
    {
      b = 1;
      setsockopt (sock_fd, IPPROTO_TCP, TCP_NODELAY, (char *) &b, sizeof (b));
    }

  success = connect (sock_fd, saddr, slen);
  if (success < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_CONNECT_JVM, 1, "connect()");
      return INVALID_SOCKET;
    }

  return sock_fd;
}

int
jsp_get_value_size (DB_VALUE * value)
{
  char str_buf[NUMERIC_MAX_STRING_SIZE];
  int type, size = 0;

  type = DB_VALUE_TYPE (value);
  switch (type)
    {
    case DB_TYPE_INTEGER:
    case DB_TYPE_SHORT:
      size = sizeof (int);
      break;

    case DB_TYPE_BIGINT:
      size = sizeof (DB_BIGINT);
      break;

    case DB_TYPE_FLOAT:
      size = sizeof (float);	/* need machine independent code */
      break;

    case DB_TYPE_DOUBLE:
    case DB_TYPE_MONETARY:
      size = sizeof (double);	/* need machine independent code */
      break;

    case DB_TYPE_NUMERIC:
      size = or_packed_string_length (numeric_db_value_print (value, str_buf), NULL);
      break;

    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_STRING:
      size = or_packed_string_length (db_get_string (value), NULL);
      break;

    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      break;

    case DB_TYPE_OBJECT:
    case DB_TYPE_DATE:
    case DB_TYPE_TIME:
      size = sizeof (int) * 3;
      break;

    case DB_TYPE_TIMESTAMP:
      size = sizeof (int) * 6;
      break;

    case DB_TYPE_DATETIME:
      size = sizeof (int) * 7;
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      {
	DB_SET *set;
	int ncol, i;
	DB_VALUE v;

	set = db_get_set (value);
	ncol = set_size (set);
	size += 4;		/* set size */

	for (i = 0; i < ncol; i++)
	  {
	    if (set_get_element (set, i, &v) != NO_ERROR)
	      {
		return 0;
	      }

	    size += jsp_get_value_size (&v);
	    pr_clear_value (&v);
	  }
      }
      break;

    case DB_TYPE_NULL:
    default:
      break;
    }

  size += 16;			/* type + value's size + mode + arg_data_type */
  return size;
}

int
jsp_get_argument_size (DB_VALUE ** argarray, const int arg_cnt)
{
  int size = 0;

  for (int i = 0; i < arg_cnt; i++)
    {
      size += jsp_get_value_size (argarray[i]);
    }

  return size;
}

int
jsp_send_call_request (THREAD_ENTRY * thread_p, DB_VALUE ** argarray, const char *name, const int arg_cnt)
{
  int error_code = NO_ERROR;
  int req_code, arg_count, i, strlen;
  int req_size, nbytes;
  char *buffer = NULL, *ptr = NULL;

  const char method_name[] = "SpPrepareTest.testWithoutJDBC(int, int) return int";

  req_size =
    (int) sizeof (int) * 4 + or_packed_string_length (method_name, &strlen) + jsp_get_argument_size (argarray, arg_cnt);

  buffer = (char *) db_private_alloc (thread_p, req_size);
  if (buffer == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) req_size);
      error_code = er_errid ();
      goto exit;
    }

  req_code = SP_CODE_INVOKE;
  ptr = or_pack_int (buffer, req_code);

  ptr = or_pack_string_with_length (ptr, method_name, strlen);

  arg_count = arg_cnt;
  ptr = or_pack_int (ptr, arg_count);

  for (int i = 0; i < arg_cnt; i++)
    {
      /*
      ptr = or_pack_int (ptr, sp_args->arg_mode[i]);
      ptr = or_pack_int (ptr, sp_args->arg_type[i]);
      ptr = jsp_pack_argument (ptr, p->val);
      */

      ptr = or_pack_int (ptr, 1); // in, arg
      ptr = or_pack_int (ptr, 1); //integer, arg
      ptr = jsp_pack_argument (ptr, argarray[i]);
    }

  ptr = or_pack_int (ptr, 1); //integer, return
  ptr = or_pack_int (ptr, req_code);

  nbytes = jsp_writen (sock_fd, buffer, req_size);
  if (nbytes != req_size)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
      error_code = er_errid ();
      goto exit;
    }

exit:
  if (buffer)
    {
      db_private_free_and_init (thread_p, buffer);
    }

  return error_code;
}

int
jsp_send_call_main (THREAD_ENTRY * thread_p, DB_VALUE ** argarray, const char *name, const int arg_cnt)
{
  int error = NO_ERROR;
  int call_cnt = 0;

retry:
  if (IS_INVALID_SOCKET (sock_fd))
    {
      sock_fd = jsp_connect_server ();
      if (IS_INVALID_SOCKET (sock_fd))
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
    }

  error = jsp_send_call_request (thread_p, argarray, name, arg_cnt);


  //error = jsp_receive_response (sock_fd, args);

end:
  call_cnt--;
  //if (error != NO_ERROR || is_prepare_call[call_cnt])
    {
      //jsp_send_destroy_request (sock_fd);
    }

  return error;
}

int
jsp_alloc_response (THREAD_ENTRY * thread_p, char *&buffer)
{
  int nbytes, res_size;
  nbytes = jsp_readn (sock_fd, (char *) &res_size, (int) sizeof (int));
  if (nbytes != (int) sizeof (int))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
      return ER_SP_NETWORK_ERROR;
    }
  res_size = ntohl (res_size);

  buffer = (char *) db_private_alloc (thread_p, res_size);
  if (!buffer)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) res_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  nbytes = jsp_readn (sock_fd, buffer, res_size);
  if (nbytes != res_size)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
      return ER_SP_NETWORK_ERROR;
    }
  return NO_ERROR;
}

int 
jsp_receive_response_main (THREAD_ENTRY * thread_p, DB_VALUE *result)
{
  int nbytes;
  int start_code = -1, end_code = -1;
  char *buffer = NULL, *ptr = NULL;
  int error_code = NO_ERROR;

redo:
  /* read request code */
  nbytes = jsp_readn (sock_fd, (char *) &start_code, (int) sizeof (int));
  if (nbytes != (int) sizeof (int))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
      return ER_SP_NETWORK_ERROR;
    }
  start_code = ntohl (start_code);

  if (start_code == SP_CODE_INTERNAL_JDBC)
    {
      //tran_begin_libcas_function ();
      //error_code = libcas_main (sockfd);	/* jdbc call */
      //tran_end_libcas_function ();
      if (error_code != NO_ERROR)
	{
	  goto exit;
	}
      goto redo;
    }
  else if (start_code == SP_CODE_RESULT || start_code == SP_CODE_ERROR)
    {
      /* read size of buffer to allocate and data */
      error_code = jsp_alloc_response (thread_p, buffer);
      if (error_code != NO_ERROR)
	{
	  goto exit;
	}

      switch (start_code)
	{
	case SP_CODE_RESULT:
    ptr = jsp_unpack_value (buffer, result);
    if (ptr == NULL)
      {
        assert (er_errid () != NO_ERROR);
        error_code = er_errid ();
        break;
      }
	  break;
	case SP_CODE_ERROR:
    DB_VALUE error_value, error_msg;

    db_make_null (result);
    ptr = jsp_unpack_value (buffer, &error_value);
    if (ptr == NULL)
      {
        assert (er_errid () != NO_ERROR);
        error_code = er_errid ();
        break;
      }

    ptr = jsp_unpack_value (ptr, &error_msg);
    if (ptr == NULL)
      {
        assert (er_errid () != NO_ERROR);
        error_code = er_errid ();
        break;
      }

    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_EXECUTE_ERROR, 1, db_get_string (&error_msg));
    error_code = er_errid ();
    db_value_clear (&error_msg);

	  break;
	}
      if (error_code != NO_ERROR)
	{
	  goto exit;
	}
      /* check request code at the end */
      if (ptr)
	{
	  ptr = or_unpack_int (ptr, &end_code);
	  if (start_code != end_code)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, end_code);
	      error_code = ER_SP_NETWORK_ERROR;
	      goto exit;
	    }
	}
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, start_code);
      error_code = ER_SP_NETWORK_ERROR;
      goto exit;
    }

exit:
  if (buffer)
    {
      db_private_free_and_init (thread_p, buffer);
    }
  return error_code;
}

int
jsp_writen (SOCKET fd, const void *vptr, int n)
{
  int nleft;
  int nwritten;
  const char *ptr;

  ptr = (const char *) vptr;
  nleft = n;

  while (nleft > 0)
    {
#if defined(WINDOWS)
      nwritten = send (fd, ptr, nleft, 0);
#else
      nwritten = send (fd, ptr, (size_t) nleft, 0);
#endif

      if (nwritten <= 0)
	{
#if defined(WINDOWS)
	  if (nwritten < 0 && errno == WSAEINTR)
#else /* not WINDOWS */
	  if (nwritten < 0 && errno == EINTR)
#endif /* not WINDOWS */
	    {
	      nwritten = 0;	/* and call write() again */
	    }
	  else
	    {
	      return (-1);	/* error */
	    }
	}

      nleft -= nwritten;
      ptr += nwritten;
    }

  return (n - nleft);
}

void
jsp_close_connection_socket ()
{
  struct linger linger_buffer;

  linger_buffer.l_onoff = 1;
  linger_buffer.l_linger = 0;
  setsockopt (sock_fd, SOL_SOCKET, SO_LINGER, (char *) &linger_buffer, sizeof (linger_buffer));
#if defined(WINDOWS)
  closesocket (sock_fd);
#else /* not WINDOWS */
  close (sock_fd);
#endif /* not WINDOWS */
}

/*
 * jsp_readn
 *   return: read size
 *   fd(in): Specifies the socket file descriptor.
 *   vptr(in/out): Points to a buffer where the message should be stored.
 *   n(in): Specifies  the  length in bytes of the buffer pointed
 *          to by the buffer argument.
 *
 * Note:
 */

int
jsp_readn (SOCKET fd, void *vptr, int n)
{
  int nleft;
  int nread;
  char *ptr;

  ptr = (char *) vptr;
  nleft = n;

  while (nleft > 0)
    {
#if defined(WINDOWS)
      nread = recv (fd, ptr, nleft, 0);
#else
      nread = recv (fd, ptr, (size_t) nleft, 0);
#endif

      if (nread < 0)
	{

#if defined(WINDOWS)
	  if (errno == WSAEINTR)
#else /* not WINDOWS */
	  if (errno == EINTR)
#endif /* not WINDOWS */
	    {
	      nread = 0;	/* and call read() again */
	    }
	  else
	    {
	      return (-1);
	    }
	}
      else if (nread == 0)
	{
	  break;		/* EOF */
	}

      nleft -= nread;
      ptr += nread;
    }

  return (n - nleft);		/* return >= 0 */
}

 char *
jsp_unpack_int_value (char *buffer, DB_VALUE * retval)
{
  int val;
  char *ptr;

  ptr = buffer;
  ptr = or_unpack_int (ptr, &val);
  db_make_int (retval, val);

  return ptr;
}

/*
 * jsp_unpack_bigint_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of bigint type
 *
 * Note:
 */

 char *
jsp_unpack_bigint_value (char *buffer, DB_VALUE * retval)
{
  DB_BIGINT val;

  memcpy ((char *) (&val), buffer, OR_BIGINT_SIZE);
  OR_GET_BIGINT (&val, &val);
  db_make_bigint (retval, val);

  return buffer + OR_BIGINT_SIZE;
}

/*
 * jsp_unpack_short_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of short type
 *
 * Note:
 */

 char *
jsp_unpack_short_value (char *buffer, DB_VALUE * retval)
{
  short val;
  char *ptr;

  ptr = buffer;
  ptr = or_unpack_short (ptr, &val);
  db_make_short (retval, val);

  return ptr;
}

/*
 * jsp_unpack_float_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of float type
 *
 * Note:
 */

 char *
jsp_unpack_float_value (char *buffer, DB_VALUE * retval)
{
  float val;
  char *ptr;

  ptr = buffer;
  ptr = or_unpack_float (ptr, &val);
  db_make_float (retval, val);

  return ptr;
}

/*
 * jsp_unpack_double_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of double type
 *
 * Note:
 */

 char *
jsp_unpack_double_value (char *buffer, DB_VALUE * retval)
{
  UINT64 val;
  double result;

  memcpy ((char *) (&val), buffer, OR_DOUBLE_SIZE);
  OR_GET_DOUBLE (&val, &result);
  db_make_double (retval, result);

  return buffer + OR_DOUBLE_SIZE;
}

/*
 * jsp_unpack_numeric_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of numeric type
 *
 * Note:
 */

 char *
jsp_unpack_numeric_value (char *buffer, DB_VALUE * retval)
{
  char *val;
  char *ptr;

  ptr = or_unpack_string_nocopy (buffer, &val);
  if (val == NULL || numeric_coerce_string_to_num (val, strlen (val), lang_charset (), retval) != NO_ERROR)
    {
      ptr = NULL;
    }

  return ptr;
}

/*
 * jsp_unpack_string_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of string type
 *
 * Note:
 */

 char *
jsp_unpack_string_value (char *buffer, DB_VALUE * retval)
{
  char *val;
  char *ptr;
  char *invalid_pos = NULL;
  int size_in;
  int composed_size;

  ptr = buffer;
  ptr = or_unpack_string (ptr, &val);

  size_in = strlen (val);

  if (intl_check_string (val, size_in, &invalid_pos, lang_charset ()) != INTL_UTF8_VALID)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_CHAR, 1, invalid_pos - val);
      return NULL;
    }

  if (lang_charset () == INTL_CODESET_UTF8 && false)
      //&& unicode_string_need_compose (val, size_in, &composed_size, lang_get_generic_unicode_norm ()))
    {
      char *composed;
      bool is_composed = false;

      composed = (char *) db_private_alloc (NULL, composed_size + 1);
      if (composed == NULL)
	{
	  return NULL;
	}

      //unicode_compose_string (val, size_in, composed, &composed_size, &is_composed, lang_get_generic_unicode_norm ());
      composed[composed_size] = '\0';

      assert (composed_size <= size_in);

      if (is_composed)
	{
	  db_private_free (NULL, val);
	  val = composed;
	}
      else
	{
	  db_private_free (NULL, composed);
	}
    }

  db_make_string (retval, val);
  //db_string_put_cs_and_collation (retval, lang_get_client_charset (), lang_get_client_collation ());
  retval->need_clear = true;

  return ptr;
}

/*
 * jsp_unpack_date_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of date type
 *
 * Note:
 */

 char *
jsp_unpack_date_value (char *buffer, DB_VALUE * retval)
{
  DB_DATE date;
  char *val;
  char *ptr;

  ptr = buffer;
  ptr = or_unpack_string_nocopy (ptr, &val);

  if (val == NULL || db_string_to_date (val, &date) != NO_ERROR)
    {
      ptr = NULL;
    }
  else
    {
      db_value_put_encoded_date (retval, &date);
    }

  return ptr;
}

/*
 * jsp_unpack_time_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of time type
 *
 * Note:
 */

 char *
jsp_unpack_time_value (char *buffer, DB_VALUE * retval)
{
  DB_TIME time;
  char *val;
  char *ptr;

  ptr = buffer;
  ptr = or_unpack_string_nocopy (ptr, &val);

  if (val == NULL || db_string_to_time (val, &time) != NO_ERROR)
    {
      ptr = NULL;
    }
  else
    {
      db_value_put_encoded_time (retval, &time);
    }

  return ptr;
}

/*
 * jsp_unpack_timestamp_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of timestamp type
 *
 * Note:
 */

 char *
jsp_unpack_timestamp_value (char *buffer, DB_VALUE * retval)
{
  DB_TIMESTAMP timestamp;
  char *val;
  char *ptr;

  ptr = buffer;
  ptr = or_unpack_string_nocopy (ptr, &val);

  if (val == NULL || db_string_to_timestamp (val, &timestamp) != NO_ERROR)
    {
      ptr = NULL;
    }
  else
    {
      db_make_timestamp (retval, timestamp);
    }

  return ptr;
}

/*
 * jsp_unpack_datetime_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of datetime type
 *
 * Note:
 */

 char *
jsp_unpack_datetime_value (char *buffer, DB_VALUE * retval)
{
  DB_DATETIME datetime;
  char *val;
  char *ptr;

  ptr = buffer;
  ptr = or_unpack_string_nocopy (ptr, &val);

  if (val == NULL || db_string_to_datetime (val, &datetime) != NO_ERROR)
    {
      ptr = NULL;
    }
  else
    {
      db_make_datetime (retval, &datetime);
    }

  return ptr;
}

/*
 * jsp_unpack_set_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of set type
 *
 * Note:
 */

 char *
jsp_unpack_set_value (char *buffer, int type, DB_VALUE * retval)
{
  DB_SET *set;
  int ncol, i;
  char *ptr;
  DB_VALUE v;

  ptr = buffer;
  ptr = or_unpack_int (ptr, &ncol);
  set = set_create ((DB_TYPE) type, ncol);

  for (i = 0; i < ncol; i++)
    {
      ptr = jsp_unpack_value (ptr, &v);
      if (ptr == NULL || set_add_element (set, &v) != NO_ERROR)
	{
	  set_free (set);
	  break;
	}
      pr_clear_value (&v);
    }

  if (type == DB_TYPE_SET)
    {
      db_make_set (retval, set);
    }
  else if (type == DB_TYPE_MULTISET)
    {
      db_make_multiset (retval, set);
    }
  else if (type == DB_TYPE_SEQUENCE)
    {
      db_make_sequence (retval, set);
    }

  return ptr;

}

/*
 * jsp_unpack_object_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of object type
 *
 * Note:
 */

 char *
jsp_unpack_object_value (char *buffer, DB_VALUE * retval)
{
  OID oid;
  MOP obj;
  char *ptr;

  ptr = buffer;
  ptr = or_unpack_int (ptr, &(oid.pageid));
  ptr = or_unpack_short (ptr, &(oid.slotid));
  ptr = or_unpack_short (ptr, &(oid.volid));

  //obj = ws_mop (&oid, NULL);
  db_make_object (retval, obj);

  return ptr;
}

/*
 * jsp_unpack_monetary_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of monetary type
 *
 * Note:
 */

char *
jsp_unpack_monetary_value (char *buffer, DB_VALUE * retval)
{
  UINT64 val;
  double result;
  char *ptr;

  ptr = buffer;
  memcpy ((char *) (&val), buffer, OR_DOUBLE_SIZE);
  OR_GET_DOUBLE (&val, &result);

  if (db_make_monetary (retval, DB_CURRENCY_DEFAULT, result) != NO_ERROR)
    {
      ptr = NULL;
    }
  else
    {
      ptr += OR_DOUBLE_SIZE;
    }

  return ptr;
}

/*
 * jsp_unpack_resultset -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of resultset type
 *
 * Note:
 */

char *
jsp_unpack_resultset (char *buffer, DB_VALUE * retval)
{
  int val;
  char *ptr;

  ptr = buffer;
  ptr = or_unpack_int (ptr, &val);
  db_make_resultset (retval, val);

  return ptr;
}

/*
 * jsp_unpack_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): db value for unpacking
 *
 * Note:
 */

char *
jsp_unpack_value (char *buffer, DB_VALUE * retval)
{
  char *ptr;
  int type;

  ptr = buffer;
  ptr = or_unpack_int (buffer, &type);

  switch (type)
    {
    case DB_TYPE_INTEGER:
      ptr = jsp_unpack_int_value (ptr, retval);
      break;

    case DB_TYPE_BIGINT:
      ptr = jsp_unpack_bigint_value (ptr, retval);
      break;

    case DB_TYPE_SHORT:
      ptr = jsp_unpack_short_value (ptr, retval);
      break;

    case DB_TYPE_FLOAT:
      ptr = jsp_unpack_float_value (ptr, retval);
      break;

    case DB_TYPE_DOUBLE:
      ptr = jsp_unpack_double_value (ptr, retval);
      break;

    case DB_TYPE_NUMERIC:
      ptr = jsp_unpack_numeric_value (ptr, retval);
      break;

    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_STRING:
      ptr = jsp_unpack_string_value (ptr, retval);
      break;

    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      break;

    case DB_TYPE_DATE:
      ptr = jsp_unpack_date_value (ptr, retval);
      break;
      /* describe_data(); */

    case DB_TYPE_TIME:
      ptr = jsp_unpack_time_value (ptr, retval);
      break;

    case DB_TYPE_TIMESTAMP:
      ptr = jsp_unpack_timestamp_value (ptr, retval);
      break;

    case DB_TYPE_DATETIME:
      ptr = jsp_unpack_datetime_value (ptr, retval);
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      ptr = jsp_unpack_set_value (ptr, type, retval);
      break;

    case DB_TYPE_OBJECT:
      ptr = jsp_unpack_object_value (ptr, retval);
      break;

    case DB_TYPE_MONETARY:
      ptr = jsp_unpack_monetary_value (ptr, retval);
      break;

    case DB_TYPE_RESULTSET:
      ptr = jsp_unpack_resultset (ptr, retval);
      break;

    case DB_TYPE_NULL:
    default:
      db_make_null (retval);
      break;
    }

  return ptr;
}