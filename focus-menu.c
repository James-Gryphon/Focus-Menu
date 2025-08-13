#include <gtk/gtk.h>
#include <glib.h>
#include <libxfce4panel/libxfce4panel.h>
#include <xfconf/xfconf.h>
#include <libxfce4ui/libxfce4ui.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
/* Required to acknowledge libwnck API instability */
// #define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>
#include <gdk/gdkx.h>
#ifndef _
#define _(String) (String)  /* Simple fallback if no i18n */
#endif

#define PLUGIN_ID "focus-menu"
#define PLUGIN_VERSION "1.0.0" // Release version
#define PLUGIN_WEBSITE "https://github.com/James-Gryphon/Focus-Menu"
#define PLUGIN_AUTHORS "James Gooch"
#define CONFIG_CHANNEL "xfce4-panel"
#define CONFIG_PROPERTY_BASE "/plugins/" PLUGIN_ID

/* CLASSIC LIBRARY DEFINES */

gchar *classlib_get_process_name_from_pid(pid_t pid);
const gchar *classlib_get_application_display_name(WnckApplication *app);
gboolean classlib_looks_like_window_title(const gchar *name);
const gchar *classlib_get_application_display_name(WnckApplication *app);
const gchar *classlib_ensure_valid_utf8(const gchar *input);
gboolean classlib_is_file_manager(WnckApplication *app);
gboolean classlib_should_blacklist_application(xmlNode *bookmark_node);
gchar *classlib_get_default_file_manager(void);
gboolean classlib_is_desktop_manager(const gchar *process_name);

typedef enum 
{
    CLASSLIB_LOCALE_TYPE_C,      /* ASCII sorting */
    CLASSLIB_LOCALE_TYPE_UTF8    /* Smart sorting that ignores special chars */
} ClassicLocaleType;

typedef enum 
{
    CLASSLIB_SORT_STYLE_CAJA,     /* Locale-aware, ignores special chars in UTF-8 */
    CLASSLIB_SORT_STYLE_THUNAR,   /* Always uses natural sorting regardless of locale */
    CLASSLIB_SORT_STYLE_UNKNOWN   /* Fallback to Caja behavior */
} ClassicSortStyle;

gint classlib_file_manager_aware_compare(const gchar *a, const gchar *b, ClassicSortStyle sort_style, ClassicLocaleType locale_type);
gint classlib_get_special_char_priority(const gchar *str, ClassicSortStyle sort_style);
ClassicLocaleType classlib_detect_locale_type(void);
gint classlib_natural_compare_strings(const gchar *a, const gchar *b);
gchar *classlib_find_desktop_file(const gchar *app_name, WnckApplication *app);
gchar *classlib_search_desktop_directory(const gchar *dir_path, const gchar *app_name);

/* END CLASS LIBRARY DEFINES*/

typedef struct 
{
    XfcePanelPlugin *plugin;
    GtkWidget *button;
    GtkWidget *label;
    GtkWidget *icon;
    GtkWidget *menu;
    WnckHandle *handle;
    WnckScreen *screen;
    WnckWindow *active_window;
    GSList *radio_group;  /* Radio group for native GTK radio menu items */
    gboolean menu_construction_mode;  /* Flag to ignore signals during menu creation */

    /* Configuration properties */
    XfconfChannel *channel;
    gchar *property_base;
    gboolean icon_only_mode;
    gboolean use_checkmarks;
    gboolean use_submenus;

    /* Sorting configuration */
    ClassicLocaleType locale_type;
    ClassicSortStyle sort_style;
} FocusMenuPlugin;

/* Structure to hold desktop manager info when no windows are detected */
typedef struct
{
    pid_t pid;
    gchar *name;
    gchar *display_name;
    gboolean is_active;
} DesktopManagerInfo;

static void update_button_display(FocusMenuPlugin *plugin);
static void create_menu(FocusMenuPlugin *plugin);
static void on_active_window_changed(WnckScreen *screen, WnckWindow *previous, FocusMenuPlugin *plugin);
static void on_window_opened(WnckScreen *screen, WnckWindow *window, FocusMenuPlugin *plugin);
static void on_window_closed(WnckScreen *screen, WnckWindow *window, FocusMenuPlugin *plugin);
static void focus_menu_free(XfcePanelPlugin *plugin);
static gboolean focus_menu_remote_event(XfcePanelPlugin *plugin, const gchar *name, const GValue *value);
static DesktopManagerInfo *desktop_manager_info_copy(const DesktopManagerInfo *src);
static void hide_current_application(GtkMenuItem *item, FocusMenuPlugin *plugin);
static void show_all_app_windows(GtkMenuItem *item, WnckApplication *app);
static void activate_single_window(GtkMenuItem *item, WnckWindow *window);
static void on_submenus_toggled(GtkToggleButton *button, FocusMenuPlugin *plugin);
static void create_flat_app_menu_item(WnckApplication *app, GList *app_window_list, gboolean is_active_app, FocusMenuPlugin *plugin);
static void create_app_submenu_with_show_all(WnckApplication *app, GList *app_window_list, gboolean is_active_app, FocusMenuPlugin *plugin);

/* Configuration functions */
static void focus_menu_configure_plugin(XfcePanelPlugin *panel, FocusMenuPlugin *plugin);
static void focus_menu_about(XfcePanelPlugin *panel);
static void on_icon_only_toggled(GtkToggleButton *button, FocusMenuPlugin *plugin);
static void on_checkmarks_toggled(GtkToggleButton *button, FocusMenuPlugin *plugin);
static void focus_menu_load_settings(FocusMenuPlugin *plugin);
static void focus_menu_save_settings(FocusMenuPlugin *plugin);
static gchar *focus_menu_get_property_name(FocusMenuPlugin *plugin, const gchar *property);
static void focus_menu_apply_icon_only_mode(FocusMenuPlugin *plugin);

/* Sorting functions */
static ClassicSortStyle determine_sort_style(WnckScreen *screen);
static gint compare_apps_by_display_name_with_data(gconstpointer a, gconstpointer b, gpointer user_data);
static gint compare_windows_by_name_with_data(gconstpointer a, gconstpointer b, gpointer user_data);
static gchar *extract_document_name_for_sorting(const gchar *window_title);

/* OPEN CLASSIC LIBRARY*/
/* Application display finder tools for PIDs */
gchar *classlib_get_process_name_from_pid(pid_t pid) 
{
    gchar *cmdline_path = g_strdup_printf("/proc/%d/cmdline", pid);
    gchar *cmdline = NULL;
    gsize cmdline_length = 0;
    gchar *result = NULL;

    if (g_file_get_contents(cmdline_path, &cmdline, &cmdline_length, NULL)) 
    {
        if (cmdline_length > 0) 
        {
            /* Extract program name (first null-terminated string) */
            gchar *program_name = g_strdup(cmdline);
            result = g_path_get_basename(program_name);
            g_free(program_name);
        }
        g_free(cmdline);
    }

    g_free(cmdline_path);
    return result ? result : g_strdup("unknown");
}

gboolean classlib_looks_like_window_title(const gchar *name)
{
    return (strlen(name) > 20 ||           // Too long for app name
    strstr(name, " — ") ||         // Contains document separator
    strstr(name, " - ") ||         // Alternate separator
    strchr(name, ':') ||           // Contains colons
    strchr(name, '/'));            // Contains paths
}

/* =============================================================================
 * APPLICATION NAME RESOLUTION SYSTEM
 * 6-tier resolution system extracted from both working projects
 * ============================================================================= */

/**
 * Validate that a string contains valid UTF-8 encoding.
 * Returns a safe fallback if the input is invalid.
 */
const gchar *classlib_ensure_valid_utf8(const gchar *input) 
{
    if (!input) 
    {
        return "Invalid App Name";
    }

    if (g_utf8_validate(input, -1, NULL)) 
    {
        return input; /* Input is valid UTF-8 */
    } 
    else 
    {
        return "Invalid App Name"; /* Safe fallback for invalid UTF-8 */
    }
}

/**
 * Get the proper display name for a WnckApplication using 6-tier resolution.
 *
 * This is the core function that both projects rely on for consistent
 * application naming. Extracted from macos9-menu.c get_app_display_name().
 */
const gchar *classlib_get_application_display_name(WnckApplication *app) 
{
    if (!app) 
    {
        return "Untitled Program";
    }

    const gchar *name = wnck_application_get_name(app);

    /* Handle applications with no name (like Python apps) */
    if (!name || strlen(name) == 0) 
    {
        /* Try to get name from first window as fallback */
        GList *windows = wnck_application_get_windows(app);
        if (windows) 
        {
            WnckWindow *first_window = WNCK_WINDOW(windows->data);
            const gchar *window_name = wnck_window_get_name(first_window);
            if (window_name && strlen(window_name) > 0) 
            {
                /* Validate UTF-8 before returning */
                return classlib_ensure_valid_utf8(window_name);
            }
        }
        /* Last resort fallback */
        return "Untitled Program";
    }

    /* Validate the application name before processing */
    name = classlib_ensure_valid_utf8(name);
    if (g_strcmp0(name, "Invalid App Name") == 0) 
    {
        return name; /* Return the safe fallback */
    }
    /* =========================================================================
     * TIER 0: Check if the name resembles a window title
     * ========================================================================= */
    /* TIER 0: PROCESS NAME FALLBACK - Handle window titles masquerading as app names */
    if (classlib_looks_like_window_title(name)) 
    {
        pid_t pid = wnck_application_get_pid(app);
        if (pid > 0)
        {
            gchar *process_name = classlib_get_process_name_from_pid(pid);
            if (process_name && strlen(process_name) > 0 && g_strcmp0(process_name, "unknown") != 0) 
                {
                /* Use process name instead and continue through existing tiers */
                name = g_intern_string(process_name);
            g_free(process_name);
                } 
                else 
                {
                    g_free(process_name);
                }
        }
    }

    /* =========================================================================
     * TIER 1: MANUAL MAPPING (Highest Priority)
     * Specific preferences and edge cases that need exact control
     * ========================================================================= */

    /* Using case-insensitive comparison for reliability */
    if (g_ascii_strcasecmp(name, "Org.mozilla.firefox") == 0) 
    {
        return "Firefox";
    } 
    else if (g_ascii_strcasecmp(name, "google-chrome") == 0) 
    {
        return "Google Chrome";
    }
     else if (g_ascii_strcasecmp(name, "code") == 0) {
        return "Visual Studio Code";
    }
     else if (g_ascii_strcasecmp(name, "vlc") == 0 || g_ascii_strcasecmp(name, "VLC media player") == 0) 
    {
        return "VLC Media Player";
    }
    else if (g_ascii_strcasecmp(name, "xfce4-about") == 0) 
    {
        return "About Xfce";
    } 
    else if (g_ascii_strcasecmp(name, "xfce4-appfinder") == 0) 
    {
        return "App Finder";
    } 
    else if (g_str_has_prefix(name, "Soffice") || g_str_has_prefix(name, "soffice"))
    {
        return "LibreOffice";
    } 
    else if (g_str_has_suffix(name, "- Audacious")) 
    {
        return "Audacious";
    }
    /* =========================================================================
        * TIER 2: XFCE SETTINGS PATTERN
        * Handle Xfce4-*-settings applications with proper capitalization
        * ========================================================================= */

    if (g_str_has_prefix(name, "Xfce4-") && g_str_has_suffix(name, "-settings")) 
    {
        /* Extract the middle part and capitalize it */
        const gchar *start = name + 6; /* Skip "Xfce4-" */
        const gchar *end = g_strrstr(name, "-settings");
        if (end && end > start) 
        {
            gsize len = end - start;
            gchar *middle = g_strndup(start, len);
            if (middle) 
            {
                /* Capitalize first letter */
                if (middle[0] >= 'a' && middle[0] <= 'z') 
                {
                    middle[0] = middle[0] - 'a' + 'A';
                }

                /* Apply Tier 5 logic to handle dashes in the middle part */
                if (strchr(middle, '-') != NULL) 
                {
                    /* Replace dashes with spaces and capitalize each word */
                    for (int i = 0; middle[i]; i++) 
                    {
                        if (middle[i] == '-') 
                        {
                            middle[i] = ' ';
                            /* Capitalize letter after space (if exists and is lowercase) */
                            if (middle[i + 1] >= 'a' && middle[i + 1] <= 'z') 
                            {
                                middle[i + 1] = middle[i + 1] - 'a' + 'A';
                            }
                        }
                    }
                }
                /* Use GLib intern string to avoid memory leaks */
                const gchar *result = g_intern_string(middle);
                g_free(middle);  /* Free our temporary string */
                return result;   /* Return the interned version (managed by GLib) */
            }
        }
    }

    /* =========================================================================
        * TIER 3: REVERSE DOMAIN PATTERN
        * Handle org.*.* and Org.*.* applications
        * ========================================================================= */

    if (g_str_has_prefix(name, "org.") || g_str_has_prefix(name, "Org.")) 
    {
        /* Find the last dot to get the app name */
        const gchar *last_dot = g_strrstr(name, ".");
        if (last_dot) 
        {
            const gchar *app_name = last_dot + 1; /* Skip the dot */
            if (strlen(app_name) > 0) 
            {
                /* Capitalize first letter only */
                gchar *capitalized = g_strdup(app_name);
                if (capitalized[0] >= 'a' && capitalized[0] <= 'z') 
                {
                    capitalized[0] = capitalized[0] - 'a' + 'A';
                }

                /* Apply Tier 5 logic if there are dashes */
                if (strchr(capitalized, '-') != NULL) 
                {
                    for (int i = 0; capitalized[i]; i++) {
                        if (capitalized[i] == '-') 
                        {
                            capitalized[i] = ' ';
                            /* Capitalize letter after space */
                            if (capitalized[i + 1] >= 'a' && capitalized[i + 1] <= 'z') 
                            {
                                capitalized[i + 1] = capitalized[i + 1] - 'a' + 'A';
                            }
                        }
                    }
                }
                const gchar *result = g_intern_string(capitalized);
                g_free(capitalized);
                return result;
            }
        }
    }

    /* =========================================================================
        * TIER 4: SIMPLE CAPITALIZATION
        * Single lowercase words only - capitalize first letter
        * ========================================================================= */

    /* Check if it's a simple single word (no spaces, dots, dashes) */
    if (!strchr(name, ' ') && !strchr(name, '.') && !strchr(name, '-')) 
    {
        /* Check if it's all lowercase */
        gboolean is_lowercase = TRUE;
        for (const gchar *p = name; *p; p++) 
        {
            if (*p >= 'A' && *p <= 'Z') 
            {
                is_lowercase = FALSE;
                break;
            }
        }

        if (is_lowercase && strlen(name) > 0) 
        {
            gchar *capitalized = g_strdup(name);
            capitalized[0] = g_ascii_toupper(capitalized[0]);
            const gchar *result = g_intern_string(capitalized);
            g_free(capitalized);
            return result;
        }
    }

    /* =========================================================================
        * TIER 5: DASH REPLACEMENT
        * Any name with dashes - replace with spaces and capitalize each word
        * ========================================================================= */

    if (strchr(name, '-') != NULL) 
    {
        gchar *processed = g_strdup(name);

        /* Replace dashes with spaces and capitalize each word */
        for (int i = 0; processed[i]; i++) 
        {
            if (processed[i] == '-') 
            {
                processed[i] = ' ';
                /* Capitalize letter after space (if exists and is lowercase) */
                if (processed[i + 1] >= 'a' && processed[i + 1] <= 'z') 
                {
                    processed[i + 1] = processed[i + 1] - 'a' + 'A';
                }
            }
        }

        /* Also capitalize the first letter if it's lowercase */
        if (processed[0] >= 'a' && processed[0] <= 'z') 
        {
            processed[0] = processed[0] - 'a' + 'A';
        }

        const gchar *result = g_intern_string(processed);
        g_free(processed);
        return result;
    }

    /* =========================================================================
        * TIER 6: FALLBACK
        * Return original name unchanged
        * ========================================================================= */

    return name;
}

/* =============================================================================
 * FILE MANAGER DETECTION AND BLACKLISTING SYSTEM
 * Extracted from switcher menu's file manager detection and spatial menu's blacklisting
 * ============================================================================= */

/**
 * Check if a process name corresponds to a desktop manager.
 * Extracted from switcher menu's desktop manager detection logic.
 */
gboolean classlib_is_desktop_manager(const gchar *process_name) 
{
    if (!process_name) 
    {
        return FALSE;
    }

    const gchar *desktop_managers[] = 
    {
        "xfdesktop",
        "caja",         /* caja -n --force-desktop */
        "nemo-desktop",
        "nautilus-desktop",
        "pcmanfm",      /* pcmanfm --desktop */
        NULL
    };

    for (int i = 0; desktop_managers[i]; i++) 
    {
        if (g_ascii_strcasecmp(process_name, desktop_managers[i]) == 0) 
        {
            return TRUE;
        }
    }
    return FALSE;
}

/**
 * Check if an application should be considered a file manager.
 * Combines desktop manager detection and traditional file manager detection.
 */
gboolean classlib_is_file_manager(WnckApplication *app) 
{
    if (!app) 
    {
        return FALSE;
    }

    const gchar *app_name = wnck_application_get_name(app);
    if (!app_name) 
    {
        return FALSE;
    }

    /* Check for known file managers */
    const gchar *file_managers[] = 
    {
        "caja",
        "thunar",
        "nemo",
        "nautilus",
        "pcmanfm",
        "dolphin",
        "konqueror",
        NULL
    };

    for (int i = 0; file_managers[i]; i++) 
    {
        if (g_ascii_strcasecmp(app_name, file_managers[i]) == 0) 
        {
            return TRUE;
        }
    }

    /* Check for desktop managers */
    return classlib_is_desktop_manager(app_name);
}

/**
 * Check if an application should be blacklisted from recent document tracking.
 * Extracted from spatial menu's is_blacklisted_application() function.
 */
/* Check if application should be blacklisted from recent documents */
gboolean classlib_should_blacklist_application(xmlNode *bookmark_node) 
{
    /* Applications that primarily download/fetch files rather than edit documents */
    const gchar *blacklisted_apps[] = 
    {
        "Firefox",
        "firefox",
        "Mozilla Firefox",
        "Chrome",
        "Chromium",
        "Google Chrome",
        "chromium",
        "wget",
        "curl",
        "Thunderbird",
        "thunderbird",
        "Transmission",
        "qBittorrent",
        "aria2c",
        "yt-dlp",
        "youtube-dl",
        NULL
    };

    /* Look for application metadata in the bookmark */
    for (xmlNode *child = bookmark_node->children; child; child = child->next) 
    {
        if (child->type != XML_ELEMENT_NODE) 
        {
            continue;
        }

        /* Check for info/metadata structure */
        if (xmlStrcmp(child->name, (const xmlChar *)"info") == 0) 
        {
            for (xmlNode *info_child = child->children; info_child; info_child = info_child->next) 
            {
                if (info_child->type != XML_ELEMENT_NODE) 
                {
                    continue;
                }

                /* Look for metadata with applications */
                if (xmlStrcmp(info_child->name, (const xmlChar *)"metadata") == 0) 
                {
                    for (xmlNode *meta_child = info_child->children; meta_child; meta_child = meta_child->next) 
                    {
                        if (meta_child->type != XML_ELEMENT_NODE) 
                        {
                            continue;
                        }

                        /* Look for bookmark:applications */
                        if (xmlStrcmp(meta_child->name, (const xmlChar *)"applications") == 0) 
                        {
                            for (xmlNode *app_child = meta_child->children; app_child; app_child = app_child->next) 
                            {
                                if (app_child->type != XML_ELEMENT_NODE) 
                                {
                                    continue;
                                }

                                /* Check bookmark:application elements */
                                if (xmlStrcmp(app_child->name, (const xmlChar *)"application") == 0) 
                                {
                                    xmlChar *app_name = xmlGetProp(app_child, (const xmlChar *)"name");
                                    if (app_name) 
                                    {
                                        /* Check against blacklist */
                                        for (int i = 0; blacklisted_apps[i]; i++) 
                                        {
                                            if (g_ascii_strcasecmp((const gchar *)app_name, blacklisted_apps[i]) == 0) 
                                            {
                                                xmlFree(app_name);
                                                return TRUE; /* Blacklisted */
                                            }
                                        }
                                        xmlFree(app_name);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return FALSE; /* Not blacklisted */
}

/**
 * Get the default file manager for the system using xdg-mime.
 */
gchar *classlib_get_default_file_manager(void) 
{
    gchar *output = NULL;
    gchar *error = NULL;
    gint exit_status;

    /* Query the default application for inode/directory */
    if (g_spawn_command_line_sync("xdg-mime query default inode/directory",
        &output, &error, &exit_status, NULL)) 
        {
            if (exit_status == 0 && output && *output) 
            {
                /* Strip newline and .desktop extension */
                g_strstrip(output);
                if (g_str_has_suffix(output, ".desktop")) 
                {
                    gchar *basename = g_path_get_basename(output);
                    gchar *name = g_strndup(basename, strlen(basename) - 8); /* Remove .desktop */
                    g_free(basename);
                    g_free(output);
                    return name;
                }
                return output;
            }
        }
        g_free(output);
        g_free(error);

        /* Fallback detection */
        const gchar *fallback_managers[] = {"caja", "thunar", "nemo", "nautilus", NULL};
        for (int i = 0; fallback_managers[i]; i++) 
        {
            gchar *path = g_find_program_in_path(fallback_managers[i]);
            if (path) 
            {
                g_free(path);
                return g_strdup(fallback_managers[i]);
            }
        }
        return NULL;
}

/* =============================================================================
 * NATURAL SORTING SYSTEM
 * File manager aware string comparison extracted from switcher menu
 * ============================================================================= */

/**
 * Detect the current system locale type for sorting purposes.
 * Follows Linux locale hierarchy: LC_ALL -> LC_COLLATE -> LANG -> "C"
 */
ClassicLocaleType classlib_detect_locale_type(void) 
{
    const gchar *locale = NULL;

    /* Follow the canonical hierarchy */
    locale = getenv("LC_ALL");
    if (!locale || !*locale) 
    {
        locale = getenv("LC_COLLATE");
        if (!locale || !*locale) 
        {
            locale = getenv("LANG");
            if (!locale || !*locale) 
            {
                locale = "C";  /* Final fallback */
            }
        }
    }

    /* Check if it's C locale */
    if (g_strcmp0(locale, "C") == 0 || g_strcmp0(locale, "POSIX") == 0) 
    {
        return CLASSLIB_LOCALE_TYPE_C;
    }

    /* Everything else is treated as UTF-8 locale */
    return CLASSLIB_LOCALE_TYPE_UTF8;
}

/**
 * Get special character priority for different file managers.
 * Extracted from switcher menu's get_special_char_priority().
 */
gint classlib_get_special_char_priority(const gchar *str, ClassicSortStyle sort_style) 
{
    if (!str || !*str) return 1;  /* Default priority for empty strings */
    {
        switch (sort_style) 
        {
            case CLASSLIB_SORT_STYLE_CAJA:
            /* Caja: Both . and # files go to end */
            if (str[0] == '.' || str[0] == '#') 
            {
                return 1;   /* Special files last */
            } 
            else 
            {
                return 0;   /* Normal files first */
            }

            case CLASSLIB_SORT_STYLE_THUNAR:
            /* Thunar: Only . files get special treatment (go to beginning) */
            if (str[0] == '.') 
            {
                return 0;   /* Hidden files first */
            } 
            else 
            {
                return 1;   /* Everything else (including #) second */
            }

            case CLASSLIB_SORT_STYLE_UNKNOWN:
            default:
            /* Default to Caja behavior */
            if (str[0] == '.' || str[0] == '#') 
            {
                return 1;
            } 
            else 
            {
                return 0;
            }
        }
    }
}

/**
 * File manager aware string comparison.
 * Extracted from switcher menu's file_manager_aware_compare().
 */
gint classlib_file_manager_aware_compare(const gchar *a, const gchar *b, ClassicSortStyle sort_style, ClassicLocaleType locale_type) 
{
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;

    /* Phase 1: Special character priority (different for each file manager) */
    gint priority_a = classlib_get_special_char_priority(a, sort_style);
    gint priority_b = classlib_get_special_char_priority(b, sort_style);

    if (priority_a != priority_b) 
    {
        return priority_a - priority_b;
    }

    /* Phase 2: Locale-aware comparison */
    if (sort_style == CLASSLIB_SORT_STYLE_CAJA && locale_type == CLASSLIB_LOCALE_TYPE_C) 
    {
        /* Only Caja falls back to C locale sorting */
        return strcmp(a, b);
    } 
    else 
    {
        /* Thunar always uses UTF-8, Caja uses UTF-8 in UTF-8 locales */
        gchar *key_a = g_utf8_collate_key_for_filename(a, -1);
        gchar *key_b = g_utf8_collate_key_for_filename(b, -1);
        gint result = strcmp(key_a, key_b);
        g_free(key_a);
        g_free(key_b);
        return result;
    }
}
/* =============================================================================
 * DESKTOP FILE SEARCH SYSTEM
 * Extracted from spatial menu's desktop file search logic
 * ============================================================================= */

/**
 * Parse desktop file for display name and icon.
 * Helper function for desktop file searching.
 */
static gboolean parse_desktop_file_for_search(const gchar *desktop_path, gchar **display_name, gchar **icon_name) 
{
    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;

    if (!g_key_file_load_from_file(key_file, desktop_path, G_KEY_FILE_NONE, &error)) 
    {
        g_key_file_free(key_file);
        if (error) g_error_free(error);
        return FALSE;
    }

    /* Get application name */
    gchar *name = g_key_file_get_string(key_file, "Desktop Entry", "Name", NULL);
    if (display_name) 
    {
        *display_name = name;
    } 
    else 
    {
        g_free(name);
    }

    /* Get icon name */
    gchar *icon = g_key_file_get_string(key_file, "Desktop Entry", "Icon", NULL);
    if (icon_name) 
    {
        *icon_name = icon;
    } 
    else 
    {
        g_free(icon);
    }

    g_key_file_free(key_file);
    return TRUE;
}

/**
 * Search a specific directory for desktop files matching an application name.
 * Extracted from spatial menu's search_desktop_dir_for_app().
 */
gchar *classlib_search_desktop_directory(const gchar *dir_path, const gchar *app_name) 
{
    if (!dir_path || !app_name) 
    {
        return NULL;
    }

    DIR *dir = opendir(dir_path);
    if (!dir) 
    {
        return NULL;
    }

    struct dirent *entry;
    gchar *result = NULL;

    while ((entry = readdir(dir)) != NULL) 
    {
        if (!g_str_has_suffix(entry->d_name, ".desktop")) 
        {
            continue;
        }

        gchar *desktop_path = g_build_filename(dir_path, entry->d_name, NULL);
        gchar *display_name = NULL;
        gchar *icon_name = NULL;

        if (parse_desktop_file_for_search(desktop_path, &display_name, &icon_name)) 
        {
            if (display_name && g_ascii_strcasecmp(display_name, app_name) == 0) 
            {
                result = g_strdup(desktop_path);
                g_free(display_name);
                g_free(icon_name);
                g_free(desktop_path);
                break;
            }
            g_free(display_name);
            g_free(icon_name);
        }
        g_free(desktop_path);
    }

    closedir(dir);
    return result;
}

/*
 * Helper function to assist classlib_find_desktop_file in retrieving executables' desktop files.
 */
static gchar *search_desktop_by_executable(const gchar *dir_path, const gchar *exe_name) 
{
    DIR *dir = opendir(dir_path);
    if (!dir) return NULL;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) 
    {
        if (!g_str_has_suffix(entry->d_name, ".desktop")) continue;

        gchar *desktop_path = g_build_filename(dir_path, entry->d_name, NULL);
        GKeyFile *keyfile = g_key_file_new();

        if (g_key_file_load_from_file(keyfile, desktop_path, G_KEY_FILE_NONE, NULL)) 
        {
            gchar *exec = g_key_file_get_string(keyfile, "Desktop Entry", "Exec", NULL);
            if (exec && strstr(exec, exe_name)) 
            {
                g_free(exec);
                g_key_file_free(keyfile);
                closedir(dir);
                return desktop_path;
            }
            g_free(exec);
        }
        g_key_file_free(keyfile);
        g_free(desktop_path);
    }
    closedir(dir);
    return NULL;
}


/**
 * Find desktop file for an application by name.
 * Extracted from spatial menu's find_desktop_file_for_application().
 */
gchar *classlib_find_desktop_file(const gchar *app_name, WnckApplication *app) 
{
    if (!app_name) return NULL;

    const gchar *desktop_dirs[] = 
    {
        "/usr/share/applications",
        "/usr/local/share/applications",
        NULL
    };

    /* Try search variations */
    gchar *search_names[4];
    search_names[0] = g_strdup(app_name);
    search_names[1] = g_ascii_strdown(app_name, -1);
    search_names[2] = g_strdelimit(g_ascii_strdown(app_name, -1), " ", '-');
    search_names[3] = NULL;

    for (int i = 0; desktop_dirs[i]; i++) 
    {
        for (int j = 0; search_names[j]; j++) 
        {
            gchar *result = classlib_search_desktop_directory(desktop_dirs[i], search_names[j]);
            if (result) 
            {
                for (int k = 0; k < 3; k++) g_free(search_names[k]);
                return result;
            }
        }
    }

    for (int i = 0; i < 3; i++) g_free(search_names[i]);
    /* Fallback: search by executable name */
    pid_t pid = wnck_application_get_pid(app);
    if (pid > 0) 
    {
        gchar *exe_name = classlib_get_process_name_from_pid(pid);
        if (exe_name && g_strcmp0(exe_name, "unknown") != 0) 
        {
            for (int i = 0; desktop_dirs[i]; i++) 
            {
                gchar *result = search_desktop_by_executable(desktop_dirs[i], exe_name);
                if (result) 
                {
                    g_free(exe_name);
                    return result;
                }
            }
        }
        g_free(exe_name);
    }
    return NULL;
}
/**
* Natural string comparison with smart number handling.
* Simplified version focusing on natural numeric ordering.
*/
gint classlib_natural_compare_strings(const gchar *a, const gchar *b) 
{
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;

    /* Use GLib's filename-aware collation which handles natural sorting */
    gchar *key_a = g_utf8_collate_key_for_filename(a, -1);
    gchar *key_b = g_utf8_collate_key_for_filename(b, -1);
    gint result = strcmp(key_a, key_b);
    g_free(key_a);
    g_free(key_b);
    return result;
}

/* END CLASSIC LIBRARY */

/* Find the WnckApplication for a given PID */
static WnckApplication *find_application_by_pid(WnckScreen *screen, pid_t pid) 
{
    if (!screen) return NULL;

    GList *windows = wnck_screen_get_windows(screen);
    for (GList *l = windows; l; l = l->next) 
    {
        WnckWindow *window = WNCK_WINDOW(l->data);
        if (!window) continue;

        WnckApplication *app = wnck_window_get_application(window);
        if (app && wnck_application_get_pid(app) == pid) 
        {
            return app;
        }
    }
    return NULL;
}

/* Enhanced find_all_desktop_managers that includes proper display names and icons */
static GList *find_all_desktop_managers(WnckScreen *screen) 
{
    GList *desktop_managers = NULL;
    GDir *proc_dir;
    const gchar *proc_entry;
    WnckWindow *active_window = NULL;

    if (screen) 
    {
        active_window = wnck_screen_get_active_window(screen);
        /* Force update to ensure we have current window information */
        wnck_screen_force_update(screen);
    }

    proc_dir = g_dir_open("/proc", 0, NULL);
    if (!proc_dir) 
    {
        g_warning("Could not open /proc directory");
        return NULL;
    }

    while ((proc_entry = g_dir_read_name(proc_dir)) != NULL) 
    {
        /* Skip non-numeric entries (not PIDs) */
        if (!g_ascii_isdigit(proc_entry[0])) 
        {
            continue;
        }

        pid_t pid = (pid_t)atoi(proc_entry);
        gchar *cmdline_path = g_strdup_printf("/proc/%s/cmdline", proc_entry);
        gchar *cmdline = NULL;
        gsize cmdline_length = 0;

        if (g_file_get_contents(cmdline_path, &cmdline, &cmdline_length, NULL)) 
        {
            if (cmdline_length > 0) 
            {
                /* Extract the program name (first argument) */
                gchar *program_name = g_strdup(cmdline);  /* First null-terminated string */
                gchar *basename = g_path_get_basename(program_name);

                /* Check if this is a known desktop manager */
                gboolean is_desktop_manager = FALSE;
                gchar *display_name = NULL;

                if (g_strcmp0(basename, "xfdesktop") == 0) 
                {
                    is_desktop_manager = TRUE;
                    display_name = g_strdup("Xfdesktop");  /* Will be updated from libwnck if available */
                } 
                else if (g_strcmp0(basename, "nemo-desktop") == 0) 
                {
                    is_desktop_manager = TRUE;
                    display_name = g_strdup("Nemo");  /* Will be updated from libwnck if available */
                } 
                else if (g_strcmp0(basename, "caja") == 0) 
                {
                    /* Check if it's the desktop version of Caja */
                    gboolean has_force_desktop = FALSE;

                    if (cmdline_length > 15) 
                    {
                        for (gsize i = 0; i <= cmdline_length - 15; i++) 
                        {
                            if (memcmp(&cmdline[i], "--force-desktop", 15) == 0) 
                            {
                                has_force_desktop = TRUE;
                                break;
                            }
                        }
                    }

                    if (has_force_desktop) 
                    {
                        is_desktop_manager = TRUE;
                        display_name = g_strdup("Caja");  /* Will be updated from libwnck if available */
                    }
                }

                if (is_desktop_manager) 
                {
                    /* Try to find the corresponding WnckApplication for better name/icon */
                    WnckApplication *app = find_application_by_pid(screen, pid);
                    if (app) 
                    {
                        /* Use the proper application display name */
                        const char *app_display_name = classlib_get_application_display_name(app);
                        if (app_display_name) 
                        {
                            g_free(display_name);
                            display_name = g_strdup(app_display_name);
                        }
                    }

                    /* Check if this desktop manager is currently active */
                    gboolean is_active = FALSE;
                    if (active_window == NULL) 
                    {
                        /* No active window means desktop is focused */
                        is_active = TRUE;
                    } 
                    else 
                    {
                        /* Check if the active window belongs to this desktop manager */
                        WnckApplication *active_app = wnck_window_get_application(active_window);
                        if (active_app && wnck_application_get_pid(active_app) == pid) 
                        {
                            is_active = TRUE;
                        }
                    }

                    DesktopManagerInfo *dm_info = g_new0(DesktopManagerInfo, 1);
                    dm_info->pid = pid;
                    dm_info->name = g_strdup(basename);
                    dm_info->display_name = display_name;  /* Transfer ownership */
                    dm_info->is_active = is_active;

                    desktop_managers = g_list_append(desktop_managers, dm_info);
                }
                g_free(program_name);
                g_free(basename);
            }
        }
        g_free(cmdline);
        g_free(cmdline_path);
    }
    g_dir_close(proc_dir);
    return desktop_managers;
}


/* Free desktop manager info */
static void desktop_manager_info_free(DesktopManagerInfo *dm_info) 
{
    if (dm_info) 
    {
        g_free(dm_info->name);
        g_free(dm_info->display_name);
        g_free(dm_info);
    }
}

/* Desktop manager detection for sorting style */
static ClassicSortStyle determine_sort_style(WnckScreen *screen) 
{
    if (!screen) 
    {
        return CLASSLIB_SORT_STYLE_CAJA;
    }

    /* Use existing desktop manager detection */
    GList *desktop_managers = find_all_desktop_managers(screen);
    ClassicSortStyle result = CLASSLIB_SORT_STYLE_UNKNOWN;

    for (GList *l = desktop_managers; l; l = l->next) 
    {
        DesktopManagerInfo *dm_info = (DesktopManagerInfo *)l->data;

        if (g_strcmp0(dm_info->name, "caja") == 0) 
        {
            result = CLASSLIB_SORT_STYLE_CAJA;
            break;
        } 
        else if (g_strcmp0(dm_info->name, "xfdesktop") == 0) 
        {
            result = CLASSLIB_SORT_STYLE_THUNAR;
            break;
        }
    }

    /* Clean up */
    g_list_free_full(desktop_managers, (GDestroyNotify)desktop_manager_info_free);

    if (result == CLASSLIB_SORT_STYLE_UNKNOWN) 
    {
        result = CLASSLIB_SORT_STYLE_CAJA;  /* Default fallback */
    }

    return result;
}

/* Helper function to remove application name suffixes from window titles */
static gchar *remove_app_name_suffix(const gchar *window_title, const gchar *app_name) 
{
    if (!window_title || !app_name) 
    {
        return g_strdup(window_title ? window_title : "");
    }

    gchar *title_copy = g_strdup(window_title);
    gsize app_len = strlen(app_name);

    /* Look for patterns like " — AppName" or " - AppName" at the end */
    const gchar *patterns[] = 
    {
        " — ",  /* Em dash U+2014 (most common) */
        " – ",  /* En dash U+2013 */
        " - ",  /* Regular hyphen-minus U+002D */
        " ― ",  /* Horizontal bar U+2015 */
        " ‒ ",  /* Figure dash U+2012 */
        " ⸺ ",  /* Two-em dash U+2E3A */
        " ⸻ ",  /* Three-em dash U+2E3B */
        "—",    /* Em dash without spaces */
        "–",    /* En dash without spaces */
        "-",    /* Hyphen without spaces */
        NULL
    };

    for (int i = 0; patterns[i]; i++) 
    {
        gsize pattern_len = strlen(patterns[i]);

        /* Find the pattern in the title */
        gchar *pattern_pos = g_strrstr(title_copy, patterns[i]);
        if (!pattern_pos) continue;

        /* Check if this pattern is followed by something that ends with our app name */
        gchar *after_pattern = pattern_pos + pattern_len;
        gsize remaining_len = strlen(after_pattern);

        /* Check for exact match with app name */
        if (remaining_len == app_len && g_ascii_strcasecmp(after_pattern, app_name) == 0) 
        {
            *pattern_pos = '\0';  /* Remove from pattern onwards */
            break;
        }

        /* Check if it ends with the app name (for cases like "Mozilla Firefox" when app_name is "Firefox") */
        if (remaining_len >= app_len) 
        {
            gchar *potential_app = after_pattern + remaining_len - app_len;
            if (g_ascii_strcasecmp(potential_app, app_name) == 0) 
            {
                /* Make sure there's a word boundary before the app name */
                if (potential_app == after_pattern || *(potential_app - 1) == ' ') 
                {
                    *pattern_pos = '\0';  /* Remove from pattern onwards */
                    break;
                }
            }
        }
    }

    /* Trim any trailing whitespace */
    g_strstrip(title_copy);

    /* If we ended up with an empty string, return a fallback */
    if (strlen(title_copy) == 0) 
    {
        g_free(title_copy);
        return g_strdup("Document");
    }

    return title_copy;
}
static gchar *ensure_valid_utf8(const gchar *input) 
{
    if (!input) 
    {
        return g_strdup("");
    }

    /* Check if string is already valid UTF-8 */
    if (g_utf8_validate(input, -1, NULL)) 
    {
        return g_strdup(input);
    }

    /* If not valid, try to make it valid by escaping invalid sequences */
    gchar *escaped = g_uri_escape_string(input, G_URI_RESERVED_CHARS_ALLOWED_IN_PATH, TRUE);
    if (escaped && g_utf8_validate(escaped, -1, NULL)) 
    {
        return escaped;
    }

    /* Last resort: return a safe fallback */
    g_free(escaped);
    return g_strdup("Invalid Window Name");
}
/* Extract document name for sorting from window titles */
static gchar *extract_document_name_for_sorting(const gchar *window_title) 
{
    if (!window_title) return g_strdup("");

    /* First ensure the input is valid UTF-8 */
    gchar *safe_title = ensure_valid_utf8(window_title);

    /* For windows like "~/path/filename - Application", extract just "filename" */
    const gchar *last_slash = g_strrstr(safe_title, "/");
    const gchar *app_separator = g_strrstr(safe_title, " - ");

    gchar *filename_part;
    if (last_slash) 
    {
        filename_part = g_strdup(last_slash + 1);  /* Skip the slash */
    } 
    else 
    {
        filename_part = g_strdup(safe_title);
    }

    /* Remove the application name suffix if present */
    if (app_separator) 
    {
        gchar *separator_in_filename = g_strrstr(filename_part, " - ");
        if (separator_in_filename) 
        {
            *separator_in_filename = '\0';  /* Truncate at the separator */
        }
    }

    g_free(safe_title);
    return filename_part;
}
/* Application sorting with file manager awareness */
static gint compare_apps_by_display_name_with_data(gconstpointer a, gconstpointer b, gpointer user_data) 
{
    WnckApplication *app_a = WNCK_APPLICATION(a);
    WnckApplication *app_b = WNCK_APPLICATION(b);
    FocusMenuPlugin *plugin = (FocusMenuPlugin *)user_data;

    const char *name_a = classlib_get_application_display_name(app_a);
    const char *name_b = classlib_get_application_display_name(app_b);

    return classlib_file_manager_aware_compare(name_a, name_b, plugin->sort_style, plugin->locale_type);
}

/* Window sorting with file manager awareness */
static gint compare_windows_by_name_with_data(gconstpointer a, gconstpointer b, gpointer user_data) 
{
    WnckWindow *win_a = WNCK_WINDOW(a);
    WnckWindow *win_b = WNCK_WINDOW(b);
    FocusMenuPlugin *plugin = (FocusMenuPlugin *)user_data;

    const char *full_name_a = wnck_window_get_name(win_a);
    const char *full_name_b = wnck_window_get_name(win_b);

    /* Handle NULL names */
    if (!full_name_a && !full_name_b) return 0;
    if (!full_name_a) return -1;
    if (!full_name_b) return 1;

    /* Extract just the filename part for sorting */
    gchar *name_a = extract_document_name_for_sorting(full_name_a);
    gchar *name_b = extract_document_name_for_sorting(full_name_b);

    gint result = classlib_file_manager_aware_compare(name_a, name_b, plugin->sort_style, plugin->locale_type);

    g_free(name_a);
    g_free(name_b);

    return result;
}

/* Helper function to apply underline styling to desktop managers */
static void apply_desktop_manager_styling(GtkWidget *menu_item) 
{
    if (!menu_item) return;

    /* Find the label widget in the menu item's box */
    GtkWidget *box = gtk_bin_get_child(GTK_BIN(menu_item));
    if (!GTK_IS_BOX(box)) return;

    GList *children = gtk_container_get_children(GTK_CONTAINER(box));
    for (GList *child = children; child; child = child->next) 
    {
        if (GTK_IS_LABEL(child->data)) 
        {
            GtkWidget *label = GTK_WIDGET(child->data);

            /* Apply underline styling to indicate special desktop manager status */
            PangoAttrList *attrs = pango_attr_list_new();
            PangoAttribute *attr = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);
            pango_attr_list_insert(attrs, attr);
            gtk_label_set_attributes(GTK_LABEL(label), attrs);
            pango_attr_list_unref(attrs);

            break;
        }
    }
    g_list_free(children);
}

/* Enhanced debug that also checks if cmdline reading is working */
static gboolean is_desktop_manager(WnckApplication *app) 
{
    if (!app) 
    {
        return FALSE;
    }

    const char *app_name = wnck_application_get_name(app);
    if (!app_name) 
    {
        return FALSE;
    }

    pid_t pid = wnck_application_get_pid(app);

    /* Direct process name checks (no cmdline parsing needed) */
    if (g_strcmp0(app_name, "nemo-desktop") == 0) 
    {
        return TRUE;
    }

    if (g_strcmp0(app_name, "xfdesktop") == 0) 
    {
        return TRUE;
    }

    /* For Caja, we need to check command line arguments */
    if (g_ascii_strcasecmp(app_name, "caja") == 0 || g_str_has_prefix(app_name, "Caja")) 
    {
        gchar *cmdline_path = g_strdup_printf("/proc/%d/cmdline", pid);
        gchar *cmdline = NULL;
        gsize cmdline_length = 0;
        gboolean is_desktop = FALSE;
        GError *error = NULL;

        if (g_file_get_contents(cmdline_path, &cmdline, &cmdline_length, &error)) 
        {
            /* Now let's manually check for the flags in the raw data */
            gboolean has_force_desktop = FALSE;
            gboolean has_n_flag = FALSE;
            gboolean has_desktop_flag = FALSE;

            /* Search for "--force-desktop" in the raw bytes */
            if (cmdline_length > 15) 
            {
                for (gsize i = 0; i <= cmdline_length - 15; i++) 
                {
                    if (memcmp(&cmdline[i], "--force-desktop", 15) == 0) 
                    {
                        has_force_desktop = TRUE;
                        break;
                    }
                }
            }

            /* Search for "-n" */
            if (cmdline_length > 2) 
            {
                for (gsize i = 0; i <= cmdline_length - 2; i++) 
                {
                    if (memcmp(&cmdline[i], "-n", 2) == 0 && (i == 0 || cmdline[i-1] == '\0') && (i + 2 >= cmdline_length || cmdline[i+2] == '\0' || cmdline[i+2] == ' ')) 
                    {
                        has_n_flag = TRUE;
                        break;
                    }
                }
            }

            /* Search for "--desktop" */
            if (cmdline_length > 9) 
            {
                for (gsize i = 0; i <= cmdline_length - 9; i++) 
                {
                    if (memcmp(&cmdline[i], "--desktop", 9) == 0) 
                    {
                        has_desktop_flag = TRUE;
                        break;
                    }
                }
            }

            if (has_force_desktop || (has_n_flag && has_force_desktop) || has_desktop_flag) 
            {
                is_desktop = TRUE;
            }
        }
        else 
        {
            if (error) g_error_free(error);
        }

        g_free(cmdline);
        g_free(cmdline_path);

        return is_desktop;
    }
    return FALSE;
}

/* Helper function to determine if a window is a desktop window (should be protected from hiding) */
static gboolean is_desktop_window(WnckWindow *window) 
{
    if (!window) return FALSE;

    /* First check: only consider NORMAL windows as potentially hideable */
    if (wnck_window_get_window_type(window) != WNCK_WINDOW_NORMAL) 
    {
        return TRUE;  /* Non-normal windows are protected */
    }

    /* Second check: skip windows that look like desktop windows by name */
    const char *window_name = wnck_window_get_name(window);
    if (window_name && (g_strcmp0(window_name, "Desktop") == 0 || g_str_has_suffix(window_name, "Desktop"))) 
    {
        return TRUE;  /* Desktop-named windows are protected */
    }
        return FALSE;  /* This window can be hidden */
}

/* Check if an application has non-desktop windows that can be hidden */
static gboolean app_has_hideable_windows(WnckApplication *app, WnckScreen *screen) 
{
    if (!app || !screen) return FALSE;

    GList *windows = wnck_application_get_windows(app);
    WnckWorkspace *active_ws = wnck_screen_get_active_workspace(screen);

    if (!active_ws) return FALSE;

    for (GList *l = windows; l; l = l->next) 
    {
        WnckWindow *window = WNCK_WINDOW(l->data);
        if (!window) continue;

        /* Check if it's on current workspace and not minimized */
        if ((wnck_window_is_visible_on_workspace(window, active_ws) || wnck_window_is_minimized(window)) && !wnck_window_is_minimized(window)) 
        {
            /* Use our helper function to check if this window can be hidden */
            if (!is_desktop_window(window)) 
            {
                return TRUE;
            }
        }
    }
    return FALSE;
}

/* Deep copy a DesktopManagerInfo structure */
static DesktopManagerInfo *desktop_manager_info_copy(const DesktopManagerInfo *src) 
{
    if (!src) return NULL;

    DesktopManagerInfo *copy = g_new0(DesktopManagerInfo, 1);
    copy->pid = src->pid;
    copy->name = g_strdup(src->name);
    copy->display_name = g_strdup(src->display_name);
    copy->is_active = src->is_active;

    return copy;
}

/* Handle desktop manager activation when clicked */
static void activate_desktop_manager(GtkMenuItem *item, gpointer user_data G_GNUC_UNUSED) 
{
    /* Check if we're in menu construction mode - if so, ignore this signal */
    GtkWidget *menu = gtk_widget_get_parent(GTK_WIDGET(item));
    FocusMenuPlugin *plugin = NULL;
    WnckScreen *screen = NULL;

    if (menu) 
    {
        plugin = g_object_get_data(G_OBJECT(menu), "plugin-data");
        if (plugin) 
        {
            if (plugin->menu_construction_mode) 
            {
                return;
            }
            screen = plugin->screen;
        }
    }

    /* Get the desktop manager info from the menu item data */
    DesktopManagerInfo *dm_info = g_object_get_data(G_OBJECT(item), "desktop-manager-info");
    if (!dm_info) 
    {
        return;
    }

    if (!screen) 
    {
        return;
    }

    /* Force libwnck to update its state before checking active window */
    wnck_screen_force_update(screen);

    WnckWindow *active_window = wnck_screen_get_active_window(screen);

    /* Only try to focus desktop if something else is currently focused */
    if (active_window) 
    {
        WnckApplication *active_app = wnck_window_get_application(active_window);

        /* Check if the current window belongs to the desktop manager */
        if (active_app && wnck_application_get_pid(active_app) == dm_info->pid) 
        {
            return;
        }

        /* Method 1: Try to find and activate a desktop window */
        GList *windows = wnck_screen_get_windows(screen);
        WnckWorkspace *active_ws = wnck_screen_get_active_workspace(screen);
        gboolean desktop_window_found = FALSE;

        if (active_ws) {
            for (GList *l = windows; l; l = l->next) 
            {
                WnckWindow *window = WNCK_WINDOW(l->data);
                if (!window) continue;

                WnckApplication *window_app = wnck_window_get_application(window);
                if (window_app && wnck_application_get_pid(window_app) == dm_info->pid) 
                {
                    WnckWindowType window_type = wnck_window_get_window_type(window);
                    const char *window_name = wnck_window_get_name(window);

                    /* Look for desktop-type windows first */
                    if (window_type == WNCK_WINDOW_DESKTOP) 
                    {
                        wnck_window_activate(window, gtk_get_current_event_time());
                        desktop_window_found = TRUE;
                        break;
                    }

                    /* Also check for windows named "Desktop" */
                    if (window_name && (g_strcmp0(window_name, "Desktop") == 0 || g_str_has_suffix(window_name, "Desktop"))) 
                    {
                        wnck_window_activate(window, gtk_get_current_event_time());
                        desktop_window_found = TRUE;
                        break;
                    }
                }
            }
        }
        /* Method 2: If no desktop window found, try to minimize current window */
        if (!desktop_window_found)
        {
            wnck_window_minimize(active_window);
        }

        /* Method 3: As a last resort, try to remove focus entirely */
        if (!desktop_window_found) 
        {
            /* Try to activate the root window (desktop) */
            GdkDisplay *display = gdk_display_get_default();
            if (display) 
            {
                GdkWindow *root_window = gdk_get_default_root_window();
                if (root_window) 
                {
                    gdk_window_focus(root_window, GDK_CURRENT_TIME);
                }
            }
        }
    }
}

static void on_submenus_toggled(GtkToggleButton *button, FocusMenuPlugin *plugin) 
{
    plugin->use_submenus = gtk_toggle_button_get_active(button);
    focus_menu_save_settings(plugin);
}

static void hide_all_applications(GtkMenuItem *item G_GNUC_UNUSED, FocusMenuPlugin *plugin) 
{
    if (!plugin || !plugin->screen) 
    {
        return;
    }

    GList *windows = wnck_screen_get_windows(plugin->screen);
    GList *apps_to_hide = NULL;
    WnckApplication *current_app = NULL;

    if (plugin->active_window) 
    {
        current_app = wnck_window_get_application(plugin->active_window);
    }

    /* Collect applications to hide, excluding current app */
    for (GList *l = windows; l; l = l->next) 
    {
        WnckWindow *window = WNCK_WINDOW(l->data);
        if (!window) continue;

        WnckWorkspace *active_ws = wnck_screen_get_active_workspace(plugin->screen);
        if (!active_ws) continue;

        if (wnck_window_is_visible_on_workspace(window, active_ws) && !wnck_window_is_minimized(window)) 
        {
            WnckApplication *app = wnck_window_get_application(window);
            if (!app || app == current_app) continue;
            /* Check if this specific window can be hidden (not the whole app) */
            if (!is_desktop_window(window)) 
            {
                /* Add to hide list if not already present */
                if (!g_list_find(apps_to_hide, app)) 
                {
                    apps_to_hide = g_list_append(apps_to_hide, app);
                }
            }
        }
    }

    /* Hide hideable windows from collected applications */
    for (GList *l = apps_to_hide; l; l = l->next) 
    {
        WnckApplication *app = WNCK_APPLICATION(l->data);
        if (!app) continue;

        GList *app_windows = wnck_application_get_windows(app);
        for (GList *w = app_windows; w; w = w->next) 
        {
            WnckWindow *window = WNCK_WINDOW(w->data);
            if (window && !wnck_window_is_minimized(window)) 
            {
                /* Double-check: only hide non-desktop windows */
                if (!is_desktop_window(window)) 
                {
                    wnck_window_minimize(window);
                }
            }
        }
    }
    g_list_free(apps_to_hide);
}

static void show_all_applications(GtkMenuItem *item G_GNUC_UNUSED, FocusMenuPlugin *plugin) 
{
    if (!plugin || !plugin->screen) 
    {
        return;
    }

    GList *windows = wnck_screen_get_windows(plugin->screen);
    guint32 timestamp = gtk_get_current_event_time();
    WnckWindow *current_active = plugin->active_window;

    /* First pass: collect minimized windows in stacking order (bottom to top) */
    GList *minimized_windows = NULL;
    for (GList *l = windows; l; l = l->next) 
    {
        WnckWindow *window = WNCK_WINDOW(l->data);
        if (!window) continue;

        WnckWorkspace *active_ws = wnck_screen_get_active_workspace(plugin->screen);
        if (!active_ws) continue;

        if ((wnck_window_is_visible_on_workspace(window, active_ws) || wnck_window_is_minimized(window)) && wnck_window_is_minimized(window) && wnck_window_get_window_type(window) == WNCK_WINDOW_NORMAL) 
        {
            /* Add to our list - this preserves the stacking order from wnck_screen_get_windows() */
            minimized_windows = g_list_append(minimized_windows, window);
        }
    }

    /* Second pass: unminimize in stacking order (bottom to top) */
    /* This preserves the original relative positions */
    for (GList *l = minimized_windows; l; l = l->next) 
    {
        WnckWindow *window = WNCK_WINDOW(l->data);
        if (window) 
        {
            wnck_window_unminimize(window, timestamp);
            /* Small delay to ensure proper stacking - some window managers need this */
            g_usleep(1000); /* 1 millisecond delay */
        }
    }

    /* Third pass: restore focus to the originally active window */
    if (current_active && !wnck_window_is_minimized(current_active)) 
    {
        wnck_window_activate(current_active, timestamp);
    }

    g_list_free(minimized_windows);
}

static void hide_current_application(GtkMenuItem *item G_GNUC_UNUSED, FocusMenuPlugin *plugin) 
{
    if (!plugin || !plugin->active_window) 
    {
        return;
    }

    WnckApplication *app = wnck_window_get_application(plugin->active_window);
    if (!app) return;

    /* Check if this is a desktop manager */
    if (is_desktop_manager(app)) 
    {
        /* For desktop managers, only hide non-desktop windows */
        GList *windows = wnck_application_get_windows(app);
        WnckWorkspace *active_ws = wnck_screen_get_active_workspace(plugin->screen);

        if (!active_ws) return;

        for (GList *l = windows; l; l = l->next) 
        {
            WnckWindow *window = WNCK_WINDOW(l->data);
            if (!window) continue;

            /* Only hide normal file manager windows, not desktop windows */
            if (wnck_window_is_visible_on_workspace(window, active_ws) && !wnck_window_is_minimized(window) && wnck_window_get_window_type(window) == WNCK_WINDOW_NORMAL) 
                {
                    /* Additional check: skip windows that look like desktop windows */
                    const char *window_name = wnck_window_get_name(window);
                    if (window_name && (g_strcmp0(window_name, "Desktop") == 0 || g_str_has_suffix(window_name, "Desktop"))) 
                    {
                        continue;
                    }
                    wnck_window_minimize(window);
                }
        }
        return;
    }

    /* Normal application - hide all windows as before */
    GList *windows = wnck_application_get_windows(app);
    for (GList *l = windows; l; l = l->next) 
    {
        WnckWindow *window = WNCK_WINDOW(l->data);
        if (window && !wnck_window_is_minimized(window)) 
        {
            wnck_window_minimize(window);
        }
    }
}

/* Enhanced helper function to apply both icon opacity and text italicization for hidden/minimized items */
static void apply_hidden_styling(GtkWidget *menu_item, gboolean is_hidden) 
{
    if (!menu_item) return;

    /* Find the icon and label widgets in the menu item's box */
    GtkWidget *box = gtk_bin_get_child(GTK_BIN(menu_item));
    if (!GTK_IS_BOX(box)) return;

    GList *children = gtk_container_get_children(GTK_CONTAINER(box));
    for (GList *child = children; child; child = child->next) 
    {
        if (GTK_IS_IMAGE(child->data)) 
        {
            /* Handle icon opacity */
            GtkWidget *icon = GTK_WIDGET(child->data);

            if (is_hidden) 
            {
                /* Apply reduced opacity for hidden items */
                gtk_widget_set_opacity(icon, 0.5);
            } 
            else 
            {
                /* Ensure normal opacity for visible items */
                gtk_widget_set_opacity(icon, 1.0);
            }
        } 
        else if (GTK_IS_LABEL(child->data)) 
        {
            /* Handle text italicization */
            GtkWidget *label = GTK_WIDGET(child->data);

            if (is_hidden) 
            {
                /* Apply italic styling for hidden items */
                PangoAttrList *attrs = pango_attr_list_new();
                PangoAttribute *attr = pango_attr_style_new(PANGO_STYLE_ITALIC);
                pango_attr_list_insert(attrs, attr);
                gtk_label_set_attributes(GTK_LABEL(label), attrs);
                pango_attr_list_unref(attrs);
            } 
            else 
            {
                /* Remove any existing attributes for visible items */
                gtk_label_set_attributes(GTK_LABEL(label), NULL);
            }
        }
    }
    g_list_free(children);
}

/* New function to create command menu items (no extra spacing needed) */
static GtkWidget* create_command_menu_item(const char* label) 
{
    if (!label) return NULL;

    /* Simple menu item - no extra spacing needed now that we use native radio buttons */
    GtkWidget *item = gtk_menu_item_new_with_label(label);

    return item;
}

/* Create menu item with radio button only if it's the active application */
static GtkWidget* create_selective_menu_item_with_icon(const char* label, GdkPixbuf* icon, gboolean is_active_app, gboolean use_checkmarks) 
{
    if (!label) return NULL;

    GtkWidget *item;

    /* Create the appropriate menu item type based on settings and active state */
    if (is_active_app) 
    {
        if (use_checkmarks) 
        {
            /* Active app with checkmarks: create check menu item */
            item = gtk_check_menu_item_new();
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);
        } 
        else 
        {
            /* Active app with radio buttons: create radio menu item */
            item = gtk_radio_menu_item_new(NULL);  /* Single item group */
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);
        }
    } 
    else 
    {
        /* Inactive apps: always regular menu items (no checkmark or radio button) */
        item = gtk_menu_item_new();
    }

    if (!item) return NULL;

    /* Create custom child widget with icon and label */
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    if (!box) 
    {
        gtk_widget_destroy(item);
        return NULL;
    }

    /* Add icon if provided */
    if (icon) 
    {
        GdkPixbuf *scaled_icon = gdk_pixbuf_scale_simple(icon, 16, 16, GDK_INTERP_BILINEAR);
        if (scaled_icon) 
        {
            GtkWidget *image = gtk_image_new_from_pixbuf(scaled_icon);
            if (image) 
            {
                gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
            }
            g_object_unref(scaled_icon);
        }
    }

    /* Add label */
    GtkWidget *label_widget = gtk_label_new(label);
    if (label_widget) 
    {
        gtk_widget_set_halign(label_widget, GTK_ALIGN_START);
        gtk_label_set_xalign(GTK_LABEL(label_widget), 0.0);
        gtk_box_pack_start(GTK_BOX(box), label_widget, TRUE, TRUE, 0);
    }

    /* Add the custom box as child of the menu item */
    gtk_container_add(GTK_CONTAINER(item), box);
    return item;
}
/* Show all windows of an application */
static void show_all_app_windows(GtkMenuItem *item, WnckApplication *app) 
{
    if (!app) return;

    /* Check if we're in menu construction mode */
    GtkWidget *menu = gtk_widget_get_parent(GTK_WIDGET(item));
    if (menu) {
        FocusMenuPlugin *plugin = g_object_get_data(G_OBJECT(menu), "plugin-data");
        if (plugin && plugin->menu_construction_mode) 
        {
            return;
        }
    }

    /* Get screen and windows */
    GList *windows = wnck_application_get_windows(app);
    if (!windows) return;

    WnckWindow *first_window = WNCK_WINDOW(windows->data);
    WnckScreen *screen = wnck_window_get_screen(first_window);
    if (!screen) return;

    guint32 timestamp = gtk_get_current_event_time();
    WnckWorkspace *active_ws = wnck_screen_get_active_workspace(screen);
    if (!active_ws) return;

    /* Collect windows and find most recent */
    GList *windows_to_show = NULL;
    WnckWindow *most_recent_window = NULL;

    for (GList *l = windows; l; l = l->next) 
    {
        WnckWindow *window = WNCK_WINDOW(l->data);
        if (!window) continue;

        /* Only process normal windows on current workspace */
        if ((wnck_window_is_visible_on_workspace(window, active_ws) || wnck_window_is_minimized(window)) && wnck_window_get_window_type(window) == WNCK_WINDOW_NORMAL) 
        {
            if (wnck_window_is_minimized(window)) 
            {
                windows_to_show = g_list_append(windows_to_show, window);
            }

            /* Try to find the most recently active window */
            if (!wnck_window_is_minimized(window)) 
            {
                /* For visible windows, prefer the currently active one */
                WnckWindow *current_active = wnck_screen_get_active_window(screen);
                if (current_active && current_active == window) 
                {
                    most_recent_window = window; /* This is currently focused */
                } 
                else if (!most_recent_window || wnck_window_is_minimized(most_recent_window)) 
                {
                    /* Use this visible window if we don't have a better candidate */
                    most_recent_window = window;
                }
            } 
            else if (!most_recent_window) 
            {
                /* If no visible window found yet, use this minimized one as fallback */
                most_recent_window = window;
            }
        }
    }

    /* First, unminimize any minimized windows */
    for (GList *l = windows_to_show; l; l = l->next) 
    {
        WnckWindow *window = WNCK_WINDOW(l->data);
        wnck_window_unminimize(window, timestamp);
        g_usleep(1000); /* Small delay for proper stacking */
    }

    /* Then activate/raise ALL windows of this app (whether they were minimized or not) */
    for (GList *l = windows; l; l = l->next) 
    {
        WnckWindow *window = WNCK_WINDOW(l->data);
        if (!window) continue;

        /* Only process normal windows on current workspace */
        if ((wnck_window_is_visible_on_workspace(window, active_ws) || wnck_window_is_minimized(window)) && wnck_window_get_window_type(window) == WNCK_WINDOW_NORMAL) 
        {
            /* Skip the most recent window - we'll activate it last */
            if (window != most_recent_window) 
            {
                wnck_window_activate(window, timestamp);
                g_usleep(500); /* Small delay between activations */
            }
        }
    }

    /* Finally, focus the most recent window (this brings it to the very top) */
    if (most_recent_window) 
    {
        wnck_window_activate(most_recent_window, timestamp);
    }

    g_list_free(windows_to_show);
}

/* Activate individual window (submenu mode only) */
static void activate_single_window(GtkMenuItem *item, WnckWindow *window) 
{
    if (!window) return;

    /* Check if we're in menu construction mode */
    GtkWidget *menu = gtk_widget_get_parent(GTK_WIDGET(item));
    if (menu) 
    {
        FocusMenuPlugin *plugin = g_object_get_data(G_OBJECT(menu), "plugin-data");
        if (plugin && plugin->menu_construction_mode) 
        {
            return; /* Ignore activation during menu construction */
        }
    }

    /* Standard window activation */
    guint32 timestamp = gtk_get_current_event_time();
    WnckWorkspace *workspace = wnck_window_get_workspace(window);

    if (workspace) 
    {
        wnck_workspace_activate(workspace, timestamp);
    }

    if (wnck_window_is_minimized(window)) 
    {
        wnck_window_unminimize(window, timestamp);
    }

    wnck_window_activate(window, timestamp);
}

/* Create single menu item for application in flat mode */
static void create_flat_app_menu_item(WnckApplication *app, GList *app_window_list, gboolean is_active_app, FocusMenuPlugin *plugin) 
{
    const char *app_name = classlib_get_application_display_name(app);
    GdkPixbuf *icon = wnck_application_get_icon(app);

    /* Check if all windows are minimized for styling */
    gboolean all_minimized = TRUE;
    for (GList *w = app_window_list; w; w = w->next) 
    {
        WnckWindow *window = WNCK_WINDOW(w->data);
        if (window && !wnck_window_is_minimized(window)) 
        {
            all_minimized = FALSE;
            break;
        }
    }

    /* Create menu item */
    GtkWidget *item = create_selective_menu_item_with_icon(app_name, icon, is_active_app, plugin->use_checkmarks);

    /* Apply styling for minimized apps */
    apply_hidden_styling(item, all_minimized);

    /* Connect to show_all_app_windows function */
    g_signal_connect(item, "activate", G_CALLBACK(show_all_app_windows), app);

    gtk_menu_shell_append(GTK_MENU_SHELL(plugin->menu), item);
}

/* Create submenu with "Show All" first item (submenu mode) */
static void create_app_submenu_with_show_all(WnckApplication *app, GList *app_window_list, gboolean is_active_app, FocusMenuPlugin *plugin) 
{
    const char *app_name = classlib_get_application_display_name(app);
    GdkPixbuf *icon = wnck_application_get_icon(app);

    /* Check if all windows are minimized for main item styling */
    gboolean all_minimized = TRUE;
    for (GList *w = app_window_list; w; w = w->next) 
    {
        WnckWindow *window = WNCK_WINDOW(w->data);
        if (window && !wnck_window_is_minimized(window)) 
        {
            all_minimized = FALSE;
            break;
        }
    }

    /* Create main menu item with submenu */
    GtkWidget *main_item = create_selective_menu_item_with_icon(app_name, icon, is_active_app, plugin->use_checkmarks);

    /* Apply styling - hidden if all windows minimized, normal if any visible */
    apply_hidden_styling(main_item, all_minimized);

    GtkWidget *submenu = gtk_menu_new();
    if (!submenu) 
    {
        g_warning("Failed to create submenu for %s", app_name);
        return;
    }

    /* First item: "Show All [AppName] Windows" */
    gchar *show_all_text = g_strdup_printf("Show All %s Windows", app_name);
    GtkWidget *show_all_item = gtk_menu_item_new_with_label(show_all_text);
    g_signal_connect(show_all_item, "activate", G_CALLBACK(show_all_app_windows), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(submenu), show_all_item);
    g_free(show_all_text);

    /* Separator */
    GtkWidget *separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(submenu), separator);

    /* Individual windows (using existing logic with modifications) */
    app_window_list = g_list_sort_with_data(app_window_list, compare_windows_by_name_with_data, plugin);

    for (GList *w = app_window_list; w; w = w->next) 
    {
        WnckWindow *window = WNCK_WINDOW(w->data);
        if (!window) continue;

        const char *window_name = wnck_window_get_name(window);
        if (!window_name) window_name = "Untitled";

        /* Ensure window name is valid UTF-8 before any processing */
        gchar *safe_window_name = ensure_valid_utf8(window_name);

        /* Remove redundant application name suffix */
        gchar *clean_window_name = remove_app_name_suffix(safe_window_name, app_name);

        /* Truncate very long window names for better usability */
        char *display_name;
        if (strlen(clean_window_name) > 50) 
        {
            /* Use g_utf8_substring to safely truncate without breaking UTF-8 characters */
            glong char_count = g_utf8_strlen(clean_window_name, -1);
            if (char_count > 47) 
            {
                gchar *truncated = g_utf8_substring(clean_window_name, 0, 47);
                display_name = g_strconcat(truncated, "...", NULL);
                g_free(truncated);
            } 
            else 
            {
                display_name = g_strdup(clean_window_name);
            }
        } 
        else 
        {
            display_name = g_strdup(clean_window_name);
        }

        /* Double-check that our final display name is valid UTF-8 */
        if (!g_utf8_validate(display_name, -1, NULL)) 
        {
            gchar *temp = display_name;
            display_name = ensure_valid_utf8(temp);
            g_free(temp);

            /* If ensure_valid_utf8 still couldn't fix it, use fallback */
            if (!display_name || strlen(display_name) == 0) 
            {
                g_free(display_name);
                display_name = g_strdup("Window");
            }
        }

        /* Create individual window menu item */
        GtkWidget *window_item = gtk_menu_item_new_with_label(display_name);

        /* Style minimized windows */
        if (wnck_window_is_minimized(window)) 
        {
            PangoAttrList *attrs = pango_attr_list_new();
            PangoAttribute *attr = pango_attr_style_new(PANGO_STYLE_ITALIC);
            pango_attr_list_insert(attrs, attr);

            GtkWidget *label = gtk_bin_get_child(GTK_BIN(window_item));
            if (GTK_IS_LABEL(label)) 
            {
                gtk_label_set_attributes(GTK_LABEL(label), attrs);
            }
            pango_attr_list_unref(attrs);
        }

        /* Connect to individual window activation */
        g_signal_connect(window_item, "activate", G_CALLBACK(activate_single_window), window);
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), window_item);

        g_free(display_name);
        g_free(clean_window_name);
        g_free(safe_window_name);
    }

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(main_item), submenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(plugin->menu), main_item);
}

/* Create menu function - the main menu construction logic */
static void create_menu(FocusMenuPlugin *plugin) 
{
    if (!plugin) 
    {
        return;
    }

    /* Destroy existing menu if it exists */
    if (plugin->menu) 
    {
        gtk_widget_destroy(plugin->menu);
        plugin->menu = NULL;
    }

    /* Reset radio group for new menu */
    if (!plugin->use_checkmarks) 
    {
        plugin->radio_group = NULL;  /* Only manage radio group when using radio buttons */
    }

    /* Enter menu construction mode to ignore activation signals */
    plugin->menu_construction_mode = TRUE;

    plugin->menu = gtk_menu_new();
    if (!plugin->menu) 
    {
        g_warning("Failed to create menu");
        plugin->menu_construction_mode = FALSE;
        return;
    }

    /* Store plugin reference in menu for signal handlers to access */
    g_object_set_data(G_OBJECT(plugin->menu), "plugin-data", plugin);

    /* Create dynamic "Hide [ApplicationName]" option */
    if (plugin->active_window) 
    {
        WnckApplication *app = wnck_window_get_application(plugin->active_window);
        if (app) 
        {
            const char *app_name = classlib_get_application_display_name(app);
            if (app_name) 
            {
                char *hide_text = g_strdup_printf("Hide %s", app_name);
                GtkWidget *hide_current = create_command_menu_item(hide_text);
                if (hide_current) 
                {
                    /* Check if this is a desktop manager */
                    gboolean is_desktop = is_desktop_manager(app);

                    if (is_desktop) 
                    {
                        /* Desktop manager - disable if no hideable windows */
                        gboolean has_hideable = app_has_hideable_windows(app, plugin->screen);
                        gtk_widget_set_sensitive(hide_current, has_hideable);
                    }

                    gtk_menu_shell_append(GTK_MENU_SHELL(plugin->menu), hide_current);
                    g_signal_connect(hide_current, "activate", G_CALLBACK(hide_current_application), plugin);
                }
                g_free(hide_text);
            }
        }
    }

    /* Check for other windows and minimized windows to determine menu item states */
    gboolean has_other_hideable = FALSE;
    gboolean has_minimized_windows = FALSE;

    GList *all_windows = wnck_screen_get_windows(plugin->screen);
    for (GList *l = all_windows; l; l = l->next) 
    {
        WnckWindow *window = WNCK_WINDOW(l->data);
        if (!window) continue;

        WnckWorkspace *active_ws = wnck_screen_get_active_workspace(plugin->screen);
        if (!active_ws) continue;

        if ((wnck_window_is_visible_on_workspace(window, active_ws) || wnck_window_is_minimized(window)) && wnck_window_get_window_type(window) == WNCK_WINDOW_NORMAL) 
        {

            if (wnck_window_is_minimized(window)) 
            {
                has_minimized_windows = TRUE;
            }

            if (window != plugin->active_window && !wnck_window_is_minimized(window)) 
            {
                WnckApplication *app = wnck_window_get_application(window);
                // To properly disable 'hide others' if there's only one program focused
                WnckApplication *current_app = wnck_window_get_application(plugin->active_window);
                /* Only count as hideable if it's not a desktop manager and not already minimized */
                if (app && !is_desktop_window(window)) 
                {
                    if(app == current_app){continue;}
                    has_other_hideable = TRUE;
                }
            }
        }
    }

    /* Add "Hide Others" and "Show All" options */
    GtkWidget *hide_others = create_command_menu_item("Hide Others");
    if (hide_others) 
    {
        gtk_widget_set_sensitive(hide_others, has_other_hideable);
        gtk_menu_shell_append(GTK_MENU_SHELL(plugin->menu), hide_others);
        g_signal_connect(hide_others, "activate", G_CALLBACK(hide_all_applications), plugin);
    }

    GtkWidget *show_all = create_command_menu_item("Show All");
    if (show_all) 
    {
        gtk_widget_set_sensitive(show_all, has_minimized_windows);
        gtk_menu_shell_append(GTK_MENU_SHELL(plugin->menu), show_all);
        g_signal_connect(show_all, "activate", G_CALLBACK(show_all_applications), plugin);
    }

    /* Add separator */
    GtkWidget *separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(plugin->menu), separator);

    /* Safety check for screen */
    if (!plugin->screen) 
    {
        g_warning("No screen available");
        plugin->menu_construction_mode = FALSE;
        gtk_widget_show_all(plugin->menu);
        return;
    }

    /* ENHANCED: Find all desktop managers (even those without visible windows) */
    GList *forced_desktop_managers = find_all_desktop_managers(plugin->screen);

    /* Get all windows and group by application (including minimized ones) */
    GList *windows = wnck_screen_get_windows(plugin->screen);
    GHashTable *app_windows = g_hash_table_new(g_direct_hash, g_direct_equal);

    /* Group windows by application with safety checks (include minimized windows) */
    for (GList *l = windows; l; l = l->next) 
    {
        WnckWindow *window = WNCK_WINDOW(l->data);
        if (!window) continue;

        WnckWorkspace *active_ws = wnck_screen_get_active_workspace(plugin->screen);
        if (!active_ws) continue;

        if ((wnck_window_is_visible_on_workspace(window, active_ws) || wnck_window_is_minimized(window)) && wnck_window_get_window_type(window) == WNCK_WINDOW_NORMAL) 
        {
            WnckApplication *app = wnck_window_get_application(window);
            if (app) 
            {
                GList *app_window_list = g_hash_table_lookup(app_windows, app);
                app_window_list = g_list_append(app_window_list, window);
                g_hash_table_replace(app_windows, app, app_window_list);
            }
        }
    }

    /* Get sorted list of applications */
    GList *apps = g_hash_table_get_keys(app_windows);
    apps = g_list_sort_with_data(apps, compare_apps_by_display_name_with_data, plugin);

    #ifdef DEBUG
    g_debug("=== DEBUG: Menu creation started ===");
    GList *debug_windows = wnck_screen_get_windows(plugin->screen);
    g_debug("DEBUG: Total windows detected: %d", g_list_length(debug_windows));

    /* Count unique applications */
    g_debug("DEBUG: Unique applications detected: %d", g_hash_table_size(app_windows));
    #endif

    /* ENHANCED: Add desktop managers first */
    for (GList *l = forced_desktop_managers; l; l = l->next) 
    {
        DesktopManagerInfo *dm_info = (DesktopManagerInfo *)l->data;

        /* Check if this desktop manager already has windows in the normal app list */
        gboolean already_in_apps = FALSE;
        for (GList *a = apps; a; a = a->next) 
        {
            WnckApplication *app = WNCK_APPLICATION(a->data);
            if (app && wnck_application_get_pid(app) == dm_info->pid) 
            {
                already_in_apps = TRUE;
                break;
            }
        }

        /* ENHANCED: For xfdesktop, also check if thunar is in the apps list */
        if (!already_in_apps && g_strcmp0(dm_info->name, "xfdesktop") == 0) 
        {
            for (GList *a = apps; a; a = a->next) 
            {
                WnckApplication *app = WNCK_APPLICATION(a->data);
                if (app) 
                {
                    const char *app_name = wnck_application_get_name(app);
                    if (app_name && g_ascii_strcasecmp(app_name, "thunar") == 0) 
                    {
                        already_in_apps = TRUE;
                        break;
                    }
                }
            }
        }

        if (!already_in_apps) 
        {
            /* This desktop manager has no visible windows - add it manually */

            /* Try to find the application for the icon */
            GdkPixbuf *desktop_icon = NULL;
            WnckApplication *desktop_app = find_application_by_pid(plugin->screen, dm_info->pid);
            if (desktop_app) 
            {
                desktop_icon = wnck_application_get_icon(desktop_app);
            }

            GtkWidget *item = create_selective_menu_item_with_icon(dm_info->display_name, desktop_icon, dm_info->is_active, plugin->use_checkmarks);
            if (item) 
            {
                /* Apply underline styling to indicate this is a special desktop manager */
                apply_desktop_manager_styling(item);
                gtk_menu_shell_append(GTK_MENU_SHELL(plugin->menu), item);

                /* Store a deep copy of dm_info in the menu item */
                DesktopManagerInfo *dm_info_copy = desktop_manager_info_copy(dm_info);
                g_object_set_data_full(G_OBJECT(item), "desktop-manager-info", dm_info_copy, (GDestroyNotify)desktop_manager_info_free);
                g_signal_connect(item, "activate", G_CALLBACK(activate_desktop_manager), NULL);
            }
        }
    }

    /* Continue with regular applications... */
    for (GList *l = apps; l; l = l->next) 
    {
        WnckApplication *app = WNCK_APPLICATION(l->data);
        if (!app) continue;

        GList *app_window_list = g_hash_table_lookup(app_windows, app);
        if (!app_window_list) continue;

        /* Always use the application name for consistency, not window names */
        const char *app_name = classlib_get_application_display_name(app);
        if (!app_name) continue;

        /* Check if this is the currently active application */
        WnckWindow *current_active = wnck_screen_get_active_window(plugin->screen);
        gboolean is_active_app = FALSE;
        if (current_active) 
        {
            WnckApplication *active_app = wnck_window_get_application(current_active);
            if (active_app == app) 
            {
                is_active_app = TRUE;
            }
        }

        if (plugin->use_submenus && g_list_length(app_window_list) > 1) 
        {
            /* Submenu mode: multi-window apps get submenus */
            create_app_submenu_with_show_all(app, app_window_list, is_active_app, plugin);
        } 
        else 
        {
            /* Flat mode: all apps get single menu item (regardless of window count) */
            create_flat_app_menu_item(app, app_window_list, is_active_app, plugin);
        }
    }
    #ifdef DEBUG
    /* Add debug version separator and info */
    GtkWidget *debug_separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(plugin->menu), debug_separator);

    GtkWidget *version_item = gtk_menu_item_new_with_label(PLUGIN_VERSION);
    if (version_item) 
    {
        gtk_widget_set_sensitive(version_item, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(plugin->menu), version_item);
    }
    #endif

    /* Exit menu construction mode - signals are now allowed */
    plugin->menu_construction_mode = FALSE;
    gtk_widget_show_all(plugin->menu);
}

static void update_button_display(FocusMenuPlugin *plugin) 
{
    if (!plugin || !plugin->screen) 
    {
        return;
    }

    plugin->active_window = wnck_screen_get_active_window(plugin->screen);

    if (plugin->active_window) 
    {
        /* Use application name for display (not window name) */
        WnckApplication *app = wnck_window_get_application(plugin->active_window);
        if (app) 
        {
            const char *app_name = classlib_get_application_display_name(app);
            GdkPixbuf *icon = wnck_application_get_icon(app);

            if (app_name) 
            {
                gtk_label_set_text(GTK_LABEL(plugin->label), app_name);
            }

            if (icon) 
            {
                GdkPixbuf *scaled_icon = gdk_pixbuf_scale_simple(icon, 16, 16, GDK_INTERP_BILINEAR);
                if (scaled_icon) 
                {
                    gtk_image_set_from_pixbuf(GTK_IMAGE(plugin->icon), scaled_icon);
                    g_object_unref(scaled_icon);
                }
            }
        }
    } 
    else 
    {
        gtk_label_set_text(GTK_LABEL(plugin->label), "Desktop");
        gtk_image_set_from_icon_name(GTK_IMAGE(plugin->icon), "desktop", GTK_ICON_SIZE_MENU);
    }
}

static gboolean on_button_pressed(GtkWidget *widget, GdkEventButton *event, FocusMenuPlugin *plugin) 
{
    if (event->button == 1) 
    { /* Left mouse button */
        create_menu(plugin);

        /* Position the menu to align right (Mac OS 9 style) */
        gtk_menu_popup_at_widget(GTK_MENU(plugin->menu), widget, GDK_GRAVITY_SOUTH_EAST, GDK_GRAVITY_NORTH_EAST, (GdkEvent*)event);
        return TRUE; /* Event handled */
    }
    return FALSE;
}

static void on_active_window_changed(WnckScreen *screen G_GNUC_UNUSED, WnckWindow *previous G_GNUC_UNUSED, FocusMenuPlugin *plugin) 
{
    update_button_display(plugin);
}

static void on_window_opened(WnckScreen *screen G_GNUC_UNUSED, WnckWindow *window G_GNUC_UNUSED, FocusMenuPlugin *plugin) 
{
    update_button_display(plugin);
}

static void on_window_closed(WnckScreen *screen G_GNUC_UNUSED, WnckWindow *window G_GNUC_UNUSED, FocusMenuPlugin *plugin) 
{
    update_button_display(plugin);
}

/* PROPERTIES DIALOG AND CONFIG FUNCTIONS */
static gchar *focus_menu_get_property_name(FocusMenuPlugin *plugin, const gchar *property) 
{
    return g_strconcat(plugin->property_base, "/", property, NULL);
}

static void focus_menu_load_settings(FocusMenuPlugin *plugin) 
{
    if (!plugin || !plugin->channel) return;

    gchar *prop_name = focus_menu_get_property_name(plugin, "icon-only");
    plugin->icon_only_mode = xfconf_channel_get_bool(plugin->channel, prop_name, FALSE);
    g_free(prop_name);

    /* Load checkmarks setting */
    prop_name = focus_menu_get_property_name(plugin, "use-checkmarks");
    plugin->use_checkmarks = xfconf_channel_get_bool(plugin->channel, prop_name, TRUE); /* Default: TRUE */
    g_free(prop_name);

    /* Load submenus setting */
    prop_name = focus_menu_get_property_name(plugin, "use-submenus");
    plugin->use_submenus = xfconf_channel_get_bool(plugin->channel, prop_name, FALSE); /* Default: FALSE */
    g_free(prop_name);
}

static void focus_menu_save_settings(FocusMenuPlugin *plugin) 
{
    if (!plugin || !plugin->channel) return;

    gchar *prop_name = focus_menu_get_property_name(plugin, "icon-only");
    xfconf_channel_set_bool(plugin->channel, prop_name, plugin->icon_only_mode);
    g_free(prop_name);

    /* Save checkmarks setting */
    prop_name = focus_menu_get_property_name(plugin, "use-checkmarks");
    xfconf_channel_set_bool(plugin->channel, prop_name, plugin->use_checkmarks);
    g_free(prop_name);

    /* Save submenus setting */
    prop_name = focus_menu_get_property_name(plugin, "use-submenus");
    xfconf_channel_set_bool(plugin->channel, prop_name, plugin->use_submenus);
    g_free(prop_name);
}

static void focus_menu_apply_icon_only_mode(FocusMenuPlugin *plugin) 
{
    if (!plugin || !plugin->label) return;

    if (plugin->icon_only_mode) 
    {
        gtk_widget_hide(plugin->label);
    } 
    else 
    {
        gtk_widget_show(plugin->label);
    }
}

static void on_icon_only_toggled(GtkToggleButton *button, FocusMenuPlugin *plugin) 
{
    plugin->icon_only_mode = gtk_toggle_button_get_active(button);
    focus_menu_save_settings(plugin);
    focus_menu_apply_icon_only_mode(plugin);
}

static void on_checkmarks_toggled(GtkToggleButton *button, FocusMenuPlugin *plugin) 
{
    plugin->use_checkmarks = gtk_toggle_button_get_active(button);
    focus_menu_save_settings(plugin);
}

static void focus_menu_configure_plugin(XfcePanelPlugin *panel, FocusMenuPlugin *plugin) 
{
    GtkWidget *dialog;
    GtkWidget *content_area;
    GtkWidget *vbox;
    GtkWidget *icon_only_check;

    /* Create dialog */
    dialog = xfce_titled_dialog_new_with_mixed_buttons(("Mac OS 9 Menu Properties"), GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(panel))), GTK_DIALOG_DESTROY_WITH_PARENT, "window-close-symbolic", _("_Close"), GTK_RESPONSE_CLOSE, NULL);

    gtk_window_set_icon_name(GTK_WINDOW(dialog), "org.xfce.panel.applicationsmenu");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 200);

    /* Get content area */
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    /* Create main vbox */
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
    gtk_box_pack_start(GTK_BOX(content_area), vbox, TRUE, TRUE, 0);

    /* Submenus mode checkbox - most important setting first */
    GtkWidget *submenus_check = gtk_check_button_new_with_label(_("Use submenus for multi-window applications"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(submenus_check), plugin->use_submenus);
    gtk_box_pack_start(GTK_BOX(vbox), submenus_check, FALSE, FALSE, 0);

    /* Icon-only mode checkbox */
    icon_only_check = gtk_check_button_new_with_label(_("Icon only (hide application name)"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(icon_only_check), plugin->icon_only_mode);
    gtk_box_pack_start(GTK_BOX(vbox), icon_only_check, FALSE, FALSE, 0);

    /* Checkmarks mode checkbox */
    GtkWidget *checkmarks_check = gtk_check_button_new_with_label(_("Use checkmarks instead of radio buttons"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkmarks_check), plugin->use_checkmarks);
    gtk_box_pack_start(GTK_BOX(vbox), checkmarks_check, FALSE, FALSE, 0);

    /* Connect signals */
    g_signal_connect(submenus_check, "toggled", G_CALLBACK(on_submenus_toggled), plugin);
    g_signal_connect(checkmarks_check, "toggled", G_CALLBACK(on_checkmarks_toggled), plugin);
    g_signal_connect(icon_only_check, "toggled", G_CALLBACK(on_icon_only_toggled), plugin);

    /* Show all widgets */
    gtk_widget_show_all(dialog);

    /* Run dialog */
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void focus_menu_about(XfcePanelPlugin *panel) 
{
    const gchar *authors[] = { PLUGIN_AUTHORS, NULL };

    gtk_show_about_dialog(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(panel))),
    "program-name", _("Focus Menu"),
    "version", PLUGIN_VERSION,
    "comments", _("A program-switching applet based on classical standards"),
    "website", PLUGIN_WEBSITE,
    "authors", authors,
    "license",_("This program is free software; you can redistribute it and/or modify it\n under the terms of the GNU General Public License as published by the Free\n Software Foundation; either version 2 of the License, or (at your option)\n any later version.\n\nThis program is distributed in the hope that it will be useful, but WITHOUT\n ANY WARRANTY; without even the implied warranty of MERCHANTABILITY\n or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for\n more details.\n\nYou should have received a copy of the GNU General Public License along with\n this program; if not, write to the Free Software Foundation, Inc., 51 Franklin\n Street, Fifth Floor, Boston, MA 02110-1301, USA."),
    "logo-icon-name", "org.xfce.panel.windowmenu",
    NULL);
}


static void focus_menu_construct(XfcePanelPlugin *plugin) 
{
    FocusMenuPlugin *focus_plugin = g_new0(FocusMenuPlugin, 1);
    focus_plugin->plugin = plugin;
    focus_plugin->radio_group = NULL;  /* Initialize radio group */
    focus_plugin->menu_construction_mode = FALSE;

    /* Initialize configuration properties */
    focus_plugin->channel = NULL;
    focus_plugin->property_base = NULL;
    focus_plugin->icon_only_mode = FALSE;  /* Default value */
    focus_plugin->use_checkmarks = TRUE;  /* Default value */
    focus_plugin->use_submenus = FALSE;   /* Default value - flat mode */

    /* Initialize locale detection */
    focus_plugin->locale_type = classlib_detect_locale_type();

    /* Create the button container */
    focus_plugin->button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(focus_plugin->button), GTK_RELIEF_NONE);

    /* Create horizontal box for icon and label */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

    /* Create icon */
    focus_plugin->icon = gtk_image_new_from_icon_name("desktop", GTK_ICON_SIZE_MENU);
    gtk_box_pack_start(GTK_BOX(hbox), focus_plugin->icon, FALSE, FALSE, 0);

    /* Create label */
    focus_plugin->label = gtk_label_new("Desktop");
    gtk_box_pack_start(GTK_BOX(hbox), focus_plugin->label, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(focus_plugin->button), hbox);

    /* Initialize libwnck with the new handle-based API */
    focus_plugin->handle = wnck_handle_new(WNCK_CLIENT_TYPE_PAGER);
    focus_plugin->screen = wnck_handle_get_default_screen(focus_plugin->handle);
    wnck_screen_force_update(focus_plugin->screen);

    /* Determine sorting style based on detected desktop manager */
    focus_plugin->sort_style = determine_sort_style(focus_plugin->screen);

    /* Initialize xfconf and load settings */
    GError *error = NULL;
    if (!xfconf_init(&error)) 
    {
        g_warning("Failed to initialize xfconf: %s", error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        /* Continue without configuration support */
        focus_plugin->channel = NULL;
    } 
    else 
    {
        focus_plugin->channel = xfconf_channel_get(CONFIG_CHANNEL);
    }

    /* Set up property base path */
    focus_plugin->property_base = g_strdup_printf("%s/plugin-%d", CONFIG_PROPERTY_BASE, xfce_panel_plugin_get_unique_id(focus_plugin->plugin));

    /* Load settings */
    focus_menu_load_settings(focus_plugin);

    /* Connect signals */
    g_signal_connect(focus_plugin->button, "button-press-event", G_CALLBACK(on_button_pressed), focus_plugin);
    g_signal_connect(focus_plugin->screen, "active-window-changed", G_CALLBACK(on_active_window_changed), focus_plugin);
    g_signal_connect(focus_plugin->screen, "window-opened", G_CALLBACK(on_window_opened), focus_plugin);
    g_signal_connect(focus_plugin->screen, "window-closed", G_CALLBACK(on_window_closed), focus_plugin);

    /* Connect plugin lifecycle signals */
    g_signal_connect(plugin, "free-data", G_CALLBACK(focus_menu_free), NULL);
    g_signal_connect(plugin, "remote-event", G_CALLBACK(focus_menu_remote_event), NULL);

    /* Set up plugin menu and connect configuration signals */
    xfce_panel_plugin_menu_show_configure(plugin);
    xfce_panel_plugin_menu_show_about(plugin);
    g_signal_connect(plugin, "configure-plugin", G_CALLBACK(focus_menu_configure_plugin), focus_plugin);
    g_signal_connect(plugin, "about", G_CALLBACK(focus_menu_about), NULL);

    /* Initial update */
    update_button_display(focus_plugin);

    /* Add to panel */
    gtk_container_add(GTK_CONTAINER(plugin), focus_plugin->button);
    xfce_panel_plugin_add_action_widget(plugin, focus_plugin->button);

    /* Store plugin data */
    g_object_set_data(G_OBJECT(plugin), "focus-plugin", focus_plugin);

    /* Show all widgets FIRST */
    gtk_widget_show_all(GTK_WIDGET(plugin));

    /* THEN apply initial settings (after widgets are shown) */
    focus_menu_apply_icon_only_mode(focus_plugin);
}

static void focus_menu_free(XfcePanelPlugin *plugin) 
{
    FocusMenuPlugin *focus_plugin = g_object_get_data(G_OBJECT(plugin), "focus-plugin");

    if (focus_plugin) 
    {
        if (focus_plugin->menu) 
        {
            gtk_widget_destroy(focus_plugin->menu);
        }

        /* Disconnect signals to avoid callbacks after cleanup */
        if (focus_plugin->screen) 
        {
            g_signal_handlers_disconnect_by_data(focus_plugin->screen, focus_plugin);
        }

        /* Clean up the wnck handle */
        if (focus_plugin->handle) 
        {
            g_object_unref(focus_plugin->handle);
        }

        /* Clean up configuration */
        if (focus_plugin->property_base) 
        {
            g_free(focus_plugin->property_base);
        }

        if (focus_plugin->channel) 
        {
            /* Channel is managed by xfconf, don't unref it */
            focus_plugin->channel = NULL;
        }
        g_free(focus_plugin);
    }
}

static gboolean focus_menu_remote_event(XfcePanelPlugin *plugin G_GNUC_UNUSED, const gchar *name G_GNUC_UNUSED, const GValue *value G_GNUC_UNUSED) 
{
    /* Handle remote events if needed */
    return FALSE;
}

XFCE_PANEL_PLUGIN_REGISTER(focus_menu_construct);
