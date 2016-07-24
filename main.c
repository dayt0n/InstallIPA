// InstallIPA - resign IPA files and install them without a jailbreak using codesign/sideloading techniques
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <zip.h>
#include <spawn.h>
#include <ctype.h>
#include "plutil.c"
#include <curl/curl.h>

const char *prg;
static void safe_create_dir(const char *dir)
{
    if (mkdir(dir, 0755) < 0) {
        if (errno != EEXIST) {
            perror(dir);
            exit(1);
        }
    }
}

void downloadFile(const char* url, const char* file_name)
{
    CURL* easyhandle = curl_easy_init();
    curl_easy_setopt( easyhandle, CURLOPT_URL, url ) ;
    FILE* file = fopen( file_name, "w");
    curl_easy_setopt( easyhandle, CURLOPT_WRITEDATA, file) ;
    curl_easy_perform( easyhandle );
    curl_easy_cleanup( easyhandle );
    fclose(file);
}

int quietUnzip(char* zipfile,char* chosenDir) {
  char cwd[1024];
  getcwd(cwd, sizeof(cwd));
  struct zip *za;
  struct zip_file *zf;
  struct zip_stat sb;
  char buf[100];
  int err;
  int i, len;
  int fd;
  long long sum;
  prg = "InstallIPA: unzip";
  chdir(chosenDir);
  if ((za = zip_open(zipfile, 0, &err)) == NULL) {
      zip_error_to_str(buf, sizeof(buf), err, errno);
      fprintf(stderr, "%s: can't open zip archive `%s': %s\n", prg,
          zipfile, buf);
      chdir(cwd);
      return 1;
  }
  for (i = 0; i < zip_get_num_entries(za, 0); i++) {

      if (zip_stat_index(za, i, 0, &sb) == 0) {
          len = strlen(sb.name);
          if (sb.name[len - 1] == '/') {
              safe_create_dir(sb.name);
          } else {
              zf = zip_fopen_index(za, i, 0);
              if (!zf) {
                  fprintf(stderr, "boese, boese\n");
                  chdir(cwd);
                  exit(100);
              }

              fd = open(sb.name, O_RDWR | O_TRUNC | O_CREAT, 0644);
              if (fd < 0) {
                  fprintf(stderr, "boese, boese\n");
                  chdir(cwd);
                  exit(101);
              }

              sum = 0;
              while (sum != sb.size) {
                  len = zip_fread(zf, buf, 100);
                  if (len < 0) {
                      fprintf(stderr, "boese, boese\n");
                      chdir(cwd);
                      exit(102);
                  }
                  write(fd, buf, len);
                  sum += len;
              }
              close(fd);
              zip_fclose(zf);
          }
      } else {
          printf("File[%s] Line[%d]\n", __FILE__, __LINE__);
      }
  }

  if (zip_close(za) == -1) {
      fprintf(stderr, "%s: can't close zip archive `%s'\n", prg, zipfile);
      return 1;
  }
  chdir(cwd);
  return -1;
}

int removeDir(char* dirname) {
  DIR *dir;
  struct dirent *entry;
  char path[PATH_MAX];

  dir = opendir(dirname);
  if (dir == NULL) {
    return 0;
  }
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
      snprintf(path, (size_t) PATH_MAX, "%s/%s", dirname, entry->d_name);
      if (entry->d_type == DT_DIR) {
        removeDir(path);
      }
      remove(path);
    }
  }
  closedir(dir);
  rmdir(dirname);
  return -1;
}

char* addVars(char *s1, char *s2)
{
    char *result = malloc(strlen(s1)+strlen(s2)+1);
    strcpy(result, s1);
    strcat(result, s2);
    return result;
}

int cp(const char *from, const char *to)
{
    int fd_to, fd_from;
    char buf[4096];
    ssize_t nread;
    int saved_errno;

    fd_from = open(from, O_RDONLY);
    if (fd_from < 0)
        return -1;

    fd_to = open(to, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd_to < 0)
        goto out_error;

    while (nread = read(fd_from, buf, sizeof buf), nread > 0)
    {
        char *out_ptr = buf;
        ssize_t nwritten;

        do {
            nwritten = write(fd_to, out_ptr, nread);

            if (nwritten >= 0)
            {
                nread -= nwritten;
                out_ptr += nwritten;
            }
            else if (errno != EINTR)
            {
                goto out_error;
            }
        } while (nread > 0);
    }

    if (nread == 0)
    {
        if (close(fd_to) < 0)
        {
            fd_to = -1;
            goto out_error;
        }
        close(fd_from);

        // Success!
        return 0;
    }

  out_error:
    saved_errno = errno; // aw :(

    close(fd_from);
    if (fd_to >= 0)
        close(fd_to);

    errno = saved_errno;
    return -1;
}

extern char **environ;
void run_cmd(char* cmd)
{
	pid_t pid;
	char *argv[] = {"sh", "-c", cmd, NULL};
	int status;
	status = posix_spawn(&pid, "/bin/sh", NULL, NULL, argv, environ);
	if (status == 0) {
		//printf("Child pid: %i\n", pid);
		if (waitpid(pid, &status, 0) != -1) {
			//printf("Child exited with status %i\n", status);
		} else {
			perror("waitpid");
		}
	} else {
		printf("posix_spawn: %s\n", strerror(status));
	}
}

void plistconv(char* infile, char* outfile) {
  char* plistargs[] = { "plutil", "-i", infile, "-o", outfile, "-d",  NULL };
  plutilmain(6,plistargs);
}

int main(int argc, char* argv[]) {
  char cwd[1024];
  getcwd(cwd, sizeof(cwd));
	removeDir("/tmp/ipawork/");
	char* tmpdir = "/tmp/ipawork/";
	if (argc <= 5) {
		printf("Incorrect usage.\nusage: %s <ipa/deb file> <provisioning profile> <bundle identifier> <team identifier> <commonname identifier>\n",argv[0]);
		return 0;
	}
	mkdir(tmpdir,0777);
  char* argoneext = strrchr(argv[1],'.');
  if (strcmp(argoneext,".deb") == 0) {
    printf("Extracting .deb file...\n");
    char* extractDeb = addVars("ar x ",argv[1]);
    chdir("/tmp/ipawork/");
    run_cmd(extractDeb);
    printf("Unpacking data...\n");
    run_cmd("tar --lzma -xf data.tar.lzma");
    run_cmd("tar -xf data.tar.gz");
    rename("Applications/","Payload/");
    remove("control.tar.gz");
    remove("data.tar.lzma");
    remove("debian-binary");
  } else if (strcmp(argoneext,".ipa") == 0) {
    printf("Extracting .ipa file...\n");
    quietUnzip(argv[1],"/tmp/ipawork/");
  } else {
    printf("File is not a .deb or .ipa. Not compatable.\n");
    exit(0);
  }
	run_cmd("ls /tmp/ipawork/Payload/ | grep \".app\" > /tmp/ipawork/appname");
	// ik, this is really messy.
	FILE* appnamefile = fopen("/tmp/ipawork/appname","r");
	fseek(appnamefile,0L,SEEK_END);
	long appnamesize = ftell(appnamefile);
	char appname[appnamesize-1];
	fseek(appnamefile,0,SEEK_SET);
	fread(&appname,1,sizeof(appname),appnamefile);
	char* newappname = appname;
	newappname[strlen(newappname)] = 0;
	printf("%s\n",newappname);
	char* appCheck = strrchr(newappname,'.');
	/* if ( strcmp(appCheck,".app") != 0) {
		printf("\aExtensions do not match... Run again. This error is bound to happen at some point.\n");
		exit(0);
	} */
	char* appFolderone = addVars("/tmp/ipawork/Payload/",newappname);
	char* appFolder = addVars(appFolderone,"/");
	fclose(appnamefile);
	remove("/tmp/ipawork/appname");
	printf("Moving provisioning profile into place...\n");
	cp(argv[2],"/tmp/ipawork/embedded.mobileprovision");
	char* devstringone = addVars("\"",argv[4]);
	char* devstringtwo = addVars(devstringone,".");
	char* devstringthree = addVars(devstringtwo,argv[3]);
	char* devstring = addVars(devstringthree,"\"");
	char* entplist = addVars("defaults write \"/tmp/ipawork/entitlements.plist\" \"application-identifier\" ",devstring);
	printf("Creating entitlements.plist...\n");
	run_cmd(entplist);
	plistconv("/tmp/ipawork/entitlements.plist","/tmp/ipawork/entitlements.plist");
	char* infoPlistLoc = addVars(appFolder,"Info.plist");
	char* infoPlistWriteOne = addVars("defaults write \"",infoPlistLoc);
	char* infoPlistWriteTwo = addVars(infoPlistWriteOne,"\" \"CFBundleIdentifier\" \"");
	char* infoPlistWriteThree = addVars(infoPlistWriteTwo,argv[3]);
	char* infoPlistWrite = addVars(infoPlistWriteThree,"\"");
	printf("Modifying CFBundleIdentifier for %s\n",newappname);
	run_cmd(infoPlistWrite);
	char* newprovisionLoc = addVars(appFolder,"embedded.mobileprovision");
	printf("Provisioning...\n");
	cp("/tmp/ipawork/embedded.mobileprovision",newprovisionLoc);
	char* codesignCmdOne = addVars("codesign --force --sign \"",argv[5]);
	char* codesignCmdTwo = addVars(codesignCmdOne,"\" --entitlements \"/tmp/ipawork/entitlements.plist\" --timestamp=none \"");
	char* codesignCmdThree = addVars(codesignCmdTwo,appFolder);
	char* codesignCmd = addVars(codesignCmdThree,"\"");
	printf("Codesigning...\n");
	run_cmd(codesignCmd);
	printf("Cleaning...\n");
	remove("/tmp/ipawork/Payload/.DS_Store");
	printf("Building IPA file...\n");
	char* ipanameone = newappname;
	ipanameone[strlen(ipanameone)-4] = 0;
	char* ipaname = addVars(ipanameone,".ipa");
	chdir("/tmp/ipawork/");
	char* zipcmdone = addVars("zip -qr ",ipaname);
	char* zipcmd = addVars(zipcmdone," Payload");
	run_cmd(zipcmd);
	// too tired to add libimobiledevice to grab UDID. maybe later, but not now.
	char* installAppOne = "ideviceinstaller -u \"`idevice_id -l`";
	char* installAppTwo = addVars(installAppOne,"\" -i /tmp/ipawork/");
	char* installApp = addVars(installAppTwo,ipaname);
	printf("Attempting to install...\n");
  char* ipaloc = addVars("/tmp/ipawork/",ipaname);
	run_cmd(installApp);
  chdir(cwd);
  cp(ipaloc,ipaname);
	removeDir("/tmp/ipawork/");
	// "it just works (sometimes)"
	return 0;
}