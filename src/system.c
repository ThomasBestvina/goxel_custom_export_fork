/* Goxel 3D voxels editor
 *
 * copyright (c) 2015 Guillaume Chereau <guillaume@noctua-software.com>
 *
 * Goxel is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.

 * Goxel is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.

 * You should have received a copy of the GNU General Public License along with
 * goxel.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "system.h"

#include <assert.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#define PATH_MAX_DIR (PATH_MAX - 128)

#define USER_PASS(...) ((const void*[]){__VA_ARGS__})
#define USER_GET(var, n) (((void**)var)[n])

// The global system instance.
sys_callbacks_t sys_callbacks = {};

// Directly include the C file!
#include "../ext_src/tinyfiledialogs/tinyfiledialogs.c"

#if defined(__unix__) && !defined(__EMSCRIPTEN__) && !defined(ANDROID)
#include <pwd.h>

static const char *get_user_dir(void)
{
    static char ret[PATH_MAX_DIR] = "";
    const char *home;
    if (!*ret) {
        home = getenv("XDG_CONFIG_HOME");
        if (home) {
            snprintf(ret, sizeof(ret), "%s/goxel", home);
        } else {
            home = getenv("HOME");
            if (!home) home = getpwuid(getuid())->pw_dir;
            snprintf(ret, sizeof(ret), "%s/.config/goxel", home);
        }
    }
    return ret;
}

#elif defined(WIN32)

// Defined in utils.
int utf_16_to_8(const wchar_t *in16, char *out8, size_t size8);

// On mingw mkdir takes only one argument!
#define mkdir(p, m) mkdir(p)

const char *get_user_dir(void)
{
    static char ret[MAX_PATH * 3 + 128] = {0};
    wchar_t knownpath_16[MAX_PATH];
    HRESULT hResult;

    if (!ret[0]) {
        hResult = SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL,
                SHGFP_TYPE_CURRENT, knownpath_16);
        if (hResult == S_OK) {
            utf_16_to_8(knownpath_16, ret, MAX_PATH * 3);
            strcat(ret, "\\Goxel\\");
        }
    }
    return ret;
}

#else

static const char *get_user_dir(void)
{
    static char ret[PATH_MAX] = "";
    const char *home;
    if (!*ret) {
        home = getenv("HOME");
        if (home)
            snprintf(ret, sizeof(ret), "%s/.config/goxel", home);
        else
            snprintf(ret, sizeof(ret), "./");
    }
    return ret;
}

#endif

void sys_log(const char *msg)
{
    if (sys_callbacks.log) {
        sys_callbacks.log(sys_callbacks.user, msg);
    } else {
        printf("%s\n", msg);
        fflush(stdout);
    }
}

// List all the files in a directory.
int sys_list_dir(const char *dirpath,
                 int (*callback)(const char *dirpath, const char *name,
                                 void *user),
                 void *user)
{
    DIR *dir;
    struct dirent *dirent;
    dir = opendir(dirpath);
    if (!dir) return -1;
    while ((dirent = readdir(dir))) {
        if (dirent->d_name[0] == '.') continue;
        if (callback(dirpath, dirent->d_name, user) != 0) break;
    }
    closedir(dir);
    return 0;
}

int sys_delete_file(const char *path)
{
    return remove(path);
}

double sys_get_time(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return (double)now.tv_sec + now.tv_usec / 1000000.0;
}

int sys_make_dir(const char *path)
{
    char tmp[PATH_MAX];
    char *p;
    strcpy(tmp, path);
    for (p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if ((mkdir(tmp, S_IRWXU) != 0) && (errno != EEXIST)) return -1;
        *p = '/';
    }
    return 0;
}

int sys_get_screen_framebuffer(void)
{
    return 0;
}

void sys_set_window_title(const char *title)
{
    static char buf[1024] = {};
    if (strcmp(buf, title) == 0) return;
    snprintf(buf, sizeof(buf), "%s", title);
    if (sys_callbacks.set_window_title)
        sys_callbacks.set_window_title(sys_callbacks.user, title);
}

/*
 * Function: sys_get_user_dir
 * Return the user config directory for goxel
 *
 * On linux, this should be $HOME/.config/goxel.
 */
const char *sys_get_user_dir(void)
{
    static char ret[PATH_MAX] = "";
    if (!*ret) {
        sys_get_path(SYS_LOCATION_CONFIG, SYS_DIR, "", ret, sizeof(ret));
    }
    return ret;
}

const char *sys_get_clipboard_text(void* user)
{
    if (sys_callbacks.get_clipboard_text)
        return sys_callbacks.get_clipboard_text(sys_callbacks.user);
    return NULL;
}

void sys_set_clipboard_text(void *user, const char *text)
{
    if (sys_callbacks.set_clipboard_text)
        sys_callbacks.set_clipboard_text(sys_callbacks.user, text);
}

/*
 * Function: sys_show_keyboard
 * Show a virtual keyboard if needed.
 */
void sys_show_keyboard(bool has_text)
{
    if (!sys_callbacks.show_keyboard) return;
    sys_callbacks.show_keyboard(sys_callbacks.user, has_text);
}

/*
 * Function: sys_save_to_photos
 * Save a png file to the system photo album.
 */
void sys_save_to_photos(const uint8_t *data, int size,
                        void (*on_finished)(int r))
{
    FILE *file;
    const char *path;
    size_t r;
    const char *filters[] = {"*.png", NULL};

    if (sys_callbacks.save_to_photos)
        return sys_callbacks.save_to_photos(sys_callbacks.user, data, size,
                                            on_finished);

    // Default implementation.
    path = sys_get_save_path("untitled.png", filters, "png");
    if (!path) {
        if (on_finished) on_finished(1);
        return;
    }
    file = fopen(path, "wb");
    if (!file) {
        if (on_finished) on_finished(-1);
        return;
    }
    r = fwrite(data, size, 1, file);
    fclose(file);
    if (on_finished) on_finished(r == size ? 0 : -1);
}

static int filters_count(const char * const*filters)
{
    int nb;
    for (nb = 0; filters[nb]; nb++) {}
    return nb;
}

const char *sys_open_file_dialog(const char *title,
                                 const char *default_path_and_file,
                                 const char * const *filters,
                                 const char *filters_desc)
{
    int nb;
    static char buf[1024];
    nb = filters_count(filters);
    if (sys_callbacks.open_dialog) {
        return sys_callbacks.open_dialog(sys_callbacks.user,
                buf, sizeof(buf), 0, title, default_path_and_file,
                nb, filters, filters_desc) ? buf : NULL;
    }
    return tinyfd_openFileDialog(title, default_path_and_file, nb,
                                 filters, filters_desc, 0);
}

const char *sys_open_folder_dialog(const char *title,
                                   const char *default_path)
{
    static char buf[1024];
    if (sys_callbacks.open_dialog) {
        return sys_callbacks.open_dialog(sys_callbacks.user,
                buf, sizeof(buf), 2, title, NULL, 0, NULL, NULL) ? buf : NULL;
    }
    return tinyfd_selectFolderDialog(title, default_path);
}

const char *sys_save_file_dialog(const char *title,
                                 const char *default_path_and_file,
                                 const char * const *filters,
                                 const char *filters_desc)
{
    int nb;
    static char buf[1024];
    nb = filters_count(filters);
    if (sys_callbacks.open_dialog) {
        return sys_callbacks.open_dialog(sys_callbacks.user,
                buf, sizeof(buf), 1, title, default_path_and_file,
                nb, filters, filters_desc) ? buf : NULL;
    }
    return tinyfd_saveFileDialog(title, default_path_and_file, nb,
                                 filters, filters_desc);
}

const char *sys_get_save_path(const char *default_name,
                              const char * const *filters,
                              const char *filters_desc)
{
    return sys_save_file_dialog("Save", default_name, filters, filters_desc);
}

void sys_on_saved(const char *path)
{
    LOG_I("Saved %s", path);
}

static int sys_get_path_(void *arg, const char *path)
{
    char *buf = USER_GET(arg, 0);
    size_t size = *(size_t*)USER_GET(arg, 1);
    assert(buf[0] == '\0');
    snprintf(buf, size, "%s", path);
    return 1;
}

int sys_get_path(int location, int options, const char *name,
                 char *buf, size_t size)
{
    buf[0] = '\0';
    sys_iter_paths(location, options, name, USER_PASS(buf, &size),
                   sys_get_path_);
    return 0;
}

static void path_join(char *buf, size_t size, const char *a, const char *b)
{
    bool need_sep;
    need_sep = (a[strlen(a) - 1] != '/') && (b[0] != '\0');
    snprintf(buf, size, "%s%s%s", a, need_sep ? "/" : "", b);
}

static bool path_exists(const char *path)
{
    struct stat path_stat;
    return stat(path, &path_stat) == 0;
}

int sys_iter_paths(int location, int options, const char *name,
                   void *arg, int (*f)(void *arg, const char *path))
{
    char base_dir[PATH_MAX_DIR];
    char path[PATH_MAX];
    int r;

    if (sys_callbacks.iter_paths) {
        return sys_callbacks.iter_paths(
                sys_callbacks.user, location, options, name, arg, f);
    }

    // Fallback.
    if (location == SYS_LOCATION_CONFIG) {
        snprintf(base_dir, sizeof(base_dir), "%s", get_user_dir());
        path_join(path, sizeof(path), base_dir, name);
        r = f(arg, path);
        if (r != 0) return r < 0 ? r : 0;

        // Also consider /etc/goxel.
        if (path_exists("/etc/goxel")) {
            path_join(path, sizeof(path), "/etc/goxel", name);
            r = f(arg, path);
            if (r != 0) return r < 0 ? r : 0;
        }

        return 0;
    }

    return -1;
}
