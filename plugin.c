/*
 * plugin.c: The VDR plugin interface
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: plugin.c 1.5 2002/05/13 16:31:09 kls Exp $
 */

#include "plugin.h"
#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <time.h>
#include "config.h"

#define LIBVDR_PREFIX  "libvdr-"
#define SO_INDICATOR   ".so."

#define MAXPLUGINARGS  1024
#define HOUSEKEEPINGDELTA 10 // seconds

// --- cPlugin ---------------------------------------------------------------

char *cPlugin::configDirectory = NULL;

cPlugin::cPlugin(void) 
{
  name = NULL;
}

cPlugin::~cPlugin()
{
  I18nRegister(NULL, Name());
}

void cPlugin::SetName(const char *s)
{
  name = s;
}

const char *cPlugin::CommandLineHelp(void)
{
  return NULL;
}

bool cPlugin::ProcessArgs(int argc, char *argv[])
{
  return true;
}

bool cPlugin::Start(void)
{
  return true;
}

void cPlugin::Housekeeping(void)
{
}

const char *cPlugin::MainMenuEntry(void)
{
  return NULL;
}

cOsdMenu *cPlugin::MainMenuAction(void)
{
  return NULL;
}

cMenuSetupPage *cPlugin::SetupMenu(void)
{
  return NULL;
}

bool cPlugin::SetupParse(const char *Name, const char *Value)
{
  return false;
}

void cPlugin::SetupStore(const char *Name, const char *Value)
{
  Setup.Store(Name, Value, this->Name());
}

void cPlugin::SetupStore(const char *Name, int Value)
{
  Setup.Store(Name, Value, this->Name());
}

void cPlugin::RegisterI18n(const tI18nPhrase * const Phrases)
{
  I18nRegister(Phrases, Name());
}

void cPlugin::SetConfigDirectory(const char *Dir)
{
  configDirectory = strdup(Dir);
}

const char *cPlugin::ConfigDirectory(const char *PluginName)
{
  static char *buffer = NULL;
  delete buffer;
  asprintf(&buffer, "%s/plugins%s%s", configDirectory, PluginName ? "/" : "", PluginName ? PluginName : "");
  return MakeDirs(buffer, true) ? buffer : NULL;
}

// --- cDll ------------------------------------------------------------------

cDll::cDll(const char *FileName, const char *Args)
{
  fileName = strdup(FileName);
  args = Args ? strdup(Args) : NULL;
  handle = NULL;
  plugin = NULL;
}

cDll::~cDll()
{
  delete plugin;
  if (handle)
     dlclose(handle);
  delete args;
  delete fileName;
}

static char *SkipQuote(char *s)
{
  char c = *s;
  strcpy(s, s + 1);
  while (*s && *s != c) {
        if (*s == '\\')
           strcpy(s, s + 1);
        if (*s)
           s++;
        }
  if (*s) {
     strcpy(s, s + 1);
     return s;
     }
  esyslog("ERROR: missing closing %c", c);
  fprintf(stderr, "vdr: missing closing %c\n", c);
  return NULL;
}

bool cDll::Load(bool Log)
{
  if (Log)
     isyslog("loading plugin: %s", fileName);
  if (handle) {
     esyslog("attempt to load plugin '%s' twice!", fileName);
     return false;
     }
  handle = dlopen(fileName, RTLD_NOW);
  const char *error = dlerror();
  if (!error) {
     void *(*creator)(void);
     (void *)creator = dlsym(handle, "VDRPluginCreator");
     if (!(error = dlerror()))
        plugin = (cPlugin *)creator();
     }
  if (!error) {
     if (plugin && args) {
        int argc = 0;
        char *argv[MAXPLUGINARGS];
        char *p = args;
        char *q = NULL;
        bool done = false;
        while (!done) {
              if (!q)
                 q = p;
              switch (*p) {
                case '\\': strcpy(p, p + 1);
                           if (*p)
                              p++;
                           else {
                              esyslog("ERROR: missing character after \\");
                              fprintf(stderr, "vdr: missing character after \\\n");
                              return false;
                              }
                           break;
                case '"':
                case '\'': if ((p = SkipQuote(p)) == NULL)
                              return false;
                           break;
                default: if (!*p || isspace(*p)) {
                            done = !*p;
                            *p = 0;
                            if (q) {
                               if (argc < MAXPLUGINARGS - 1)
                                  argv[argc++] = q;
                               else {
                                  esyslog("ERROR: plugin argument list too long");
                                  fprintf(stderr, "vdr: plugin argument list too long\n");
                                  return false;
                                  }
                               q = NULL;
                               }
                            }
                         if (!done)
                            p++;
                }
              }
        argv[argc] = NULL;
        if (argc)
           plugin->SetName(argv[0]);
        optind = 0; // to reset the getopt() data
        return !argc || plugin->ProcessArgs(argc, argv);
        }
     }
  else {
     esyslog("ERROR: %s", error);
     fprintf(stderr, "vdr: %s\n", error);
     }
  return !error && plugin;
}

// --- cPluginManager --------------------------------------------------------

cPluginManager *cPluginManager::pluginManager = NULL;

cPluginManager::cPluginManager(const char *Directory)
{
  directory = NULL;
  lastHousekeeping = time(NULL);
  nextHousekeeping = -1;
  if (pluginManager) {
     fprintf(stderr, "vdr: attempt to create more than one plugin manager - exiting!\n");
     exit(2);
     }
  SetDirectory(Directory);
  pluginManager = this;
}

cPluginManager::~cPluginManager()
{
  Shutdown();
  delete directory;
  if (pluginManager == this)
     pluginManager = NULL;
}

void cPluginManager::SetDirectory(const char *Directory)
{
  delete directory;
  directory = Directory ? strdup(Directory) : NULL;
}

void cPluginManager::AddPlugin(const char *Args)
{
  if (strcmp(Args, "*") == 0) {
     DIR *d = opendir(directory);
     if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
              if (strstr(e->d_name, LIBVDR_PREFIX) == e->d_name) {
                 char *p = strstr(e->d_name, SO_INDICATOR);
                 if (p) {
                    *p = 0;
                    p += strlen(SO_INDICATOR);
                    if (strcmp(p, VDRVERSION) == 0) {
                       char *name = e->d_name + strlen(LIBVDR_PREFIX);
                       if (strcmp(name, "*") != 0) { // let's not get into a loop!
                          AddPlugin(e->d_name + strlen(LIBVDR_PREFIX));
                          }
                       }
                    }
                 }
              }
        closedir(d);
        }
     return;
     }
  char *s = strdup(Args);
  char *p = strchr(s, ' ');
  if (p)
     *p = 0;
  char *buffer = NULL;
  asprintf(&buffer, "%s/%s%s%s%s", directory, LIBVDR_PREFIX, s, SO_INDICATOR, VDRVERSION);
  dlls.Add(new cDll(buffer, Args));
  delete buffer;
  delete s;
}

bool cPluginManager::LoadPlugins(bool Log)
{
  for (cDll *dll = dlls.First(); dll; dll = dlls.Next(dll)) {
      if (!dll->Load(Log))
         return false;
      }
  return true;
}

bool cPluginManager::StartPlugins(void)
{
  for (cDll *dll = dlls.First(); dll; dll = dlls.Next(dll)) {
      cPlugin *p = dll->Plugin();
      if (p) {
         int Language = Setup.OSDLanguage;
         Setup.OSDLanguage = 0; // the i18n texts are only available _after_ Start()
         isyslog("starting plugin: %s (%s): %s", p->Name(), p->Version(), p->Description());
         Setup.OSDLanguage = Language;
         if (!p->Start())
            return false;
         }
      }
  return true;
}

void cPluginManager::Housekeeping(void)
{
  if (time(NULL) - lastHousekeeping > HOUSEKEEPINGDELTA) {
     if (++nextHousekeeping >= dlls.Count())
        nextHousekeeping = 0;
     cDll *dll = dlls.Get(nextHousekeeping);
     if (dll) {
        cPlugin *p = dll->Plugin();
        if (p) {
           p->Housekeeping();
           }
        }
     lastHousekeeping = time(NULL);
     }
}

bool cPluginManager::HasPlugins(void)
{
  return pluginManager && pluginManager->dlls.Count();
}

cPlugin *cPluginManager::GetPlugin(int Index)
{
  cDll *dll = pluginManager ? pluginManager->dlls.Get(Index) : NULL;
  return dll ? dll->Plugin() : NULL;
}

cPlugin *cPluginManager::GetPlugin(const char *Name)
{
  if (pluginManager) {
     for (cDll *dll = pluginManager->dlls.First(); dll; dll = pluginManager->dlls.Next(dll)) {
         cPlugin *p = dll->Plugin();
         if (p && strcmp(p->Name(), Name) == 0)
            return p;
         }
     }
  return NULL;
}

void cPluginManager::Shutdown(bool Log)
{
  cDll *dll;
  while ((dll = dlls.Last()) != NULL) {
        if (Log) {
           cPlugin *p = dll->Plugin();
           if (p)
              isyslog("stopping plugin: %s", p->Name());
           }
        dlls.Del(dll);
        }
}