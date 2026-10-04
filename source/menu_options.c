#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <mini18n.h>

#include "saves.h"
#include "menu.h"
#include "menu_gui.h"
#include "libfont.h"
#include "orbisPad.h"
#include "google_drive.h"

static int google_panel;
static unsigned google_selection;
static size_t google_text_prefix(const char *text, size_t limit)
{
    size_t n = strlen(text);
    if (n <= limit) return n;
    n = limit;
    while (n && ((unsigned char)text[n] & 0xc0) == 0x80) n--;
    return n;
}
void google_drive_ui_upload(const char *game, const char *title, const char *directory, uint32_t user)
{
    if (!google_drive_upload_start(game,title,directory,user)) {
        show_message("Unable to start Google Drive backup."); return;
    }
    google_panel = 1;
}
static void google_text(const char *text, int y)
{
    char line[61];
    while (*text) {
        size_t n = google_text_prefix(text,60);
        const char *newline = memchr(text, '\n', n);
        if (newline) n = (size_t)(newline - text);
        memcpy(line, text, n); line[n] = 0;
        DrawString(180, y, line);
        y += 35; text += n;
        if (*text == '\n') text++;
    }
}
void google_drive_ui_start(int action)
{
    google_selection = 0;
    if (!google_drive_start(action, apollo_config.user_id)) {
        show_message("Unable to start Google Drive operation.");
        return;
    }
    google_panel = 1;
}

int google_drive_ui_frame(void)
{
    google_drive_status status;
    if (!google_panel) return 0;
    google_drive_snapshot(&status);
    if (!status.busy && status.browsing) {
        if (google_selection >= status.backups.count) google_selection = 0;
        if (orbisPadGetButtonPressed(ORBIS_PAD_BUTTON_UP) && google_selection) google_selection--;
        if (orbisPadGetButtonPressed(ORBIS_PAD_BUTTON_DOWN) && google_selection + 1 < status.backups.count) google_selection++;
        if (orbisPadGetButtonPressed(ORBIS_PAD_BUTTON_R1) && status.backups.next[0]) google_drive_ui_start(GOOGLE_NEXT);
        if (orbisPadGetButtonPressed(ORBIS_PAD_BUTTON_CROSS) && status.backups.count &&
            !google_drive_download_start(google_selection,apollo_config.user_id))
            show_message("Unable to start Google Drive download.");
    }
    if (orbisPadGetButtonPressed(ORBIS_PAD_BUTTON_CIRCLE)) {
        if (status.mount_blocked) { /* Require restart after an unconfirmed unmount. */ }
        else if (status.busy && status.cancellable) google_drive_cancel();
        else if (status.busy) { /* Let atomic credential updates finish. */ }
        else google_panel = 0;
    }
    DrawHeader(cat_opt_png_index, 0, "Google Drive", NULL, APP_FONT_TITLE_COLOR | 0xFF, 0xffffffff, 0);
    SetFontAlign(FONT_ALIGN_LEFT);
    SetFontSize(28, 32);
    SetFontColor(APP_FONT_COLOR | 0xFF, 0);
    /* Wrap sanitized text; reserve generous width for Google's returned values. */
    google_text(status.message, 250);
    if (status.preparation_details[0]) google_text(status.preparation_details, 440);
    if (status.total_bytes)
        DrawFormatString(180, 370, "Transfer: %llu / %llu bytes (%u%%)",
            (unsigned long long)status.completed_bytes, (unsigned long long)status.total_bytes,
            (unsigned)(100.0 * status.completed_bytes / status.total_bytes));
    if (status.discovery_details[0]) google_text(status.discovery_details, 440);
    if (!status.busy && status.browsing) {
        for (unsigned i=0;i<status.backups.count;i++) {
            const google_backup *b=&status.backups.entries[i].backup;
            /* All remote values are arguments, never format strings. */
            DrawFormatString(180,350+i*32,"%s [%s] %s  %.*s",i==google_selection?">":" ",b->title,b->utc,
                (int)google_text_prefix(b->game,28),b->game);
        }
        if (status.backups.count) {
            const google_backup *b=&status.backups.entries[google_selection].backup;
            DrawFormatString(180,690,"Game: %.*s",(int)google_text_prefix(b->game,60),b->game);
            DrawFormatString(180,725,"Save: %s",b->directory);
            DrawFormatString(180,755,"Backup: %s  Size: %llu bytes",b->utc,(unsigned long long)b->size);
        }
        DrawString(180,820,"Up/Down: select  Confirm: download  R1: next page");
    }
    if (status.verification_url[0]) {
        DrawString(180, 440, "Verification URL:");
        google_text(status.verification_url, 480);
        DrawString(180, 680, "User code (case sensitive):");
        google_text(status.user_code, 720);
    }
    const char *cancel_button = orbisPadGetConf()->crossButtonOK ? "Circle" : "Cross";
    if (status.mount_blocked)
        DrawString(180, 900, "Restart the app to release the failed mount.");
    else if (status.busy && !status.cancellable)
        DrawString(180, 900, "Finishing local credential update...");
    else
        DrawFormatString(180, 900, "%s: %s", cancel_button,
            status.busy ? "cancel operation" : "return");
    return 1;
}

static void _draw_OptionsMenu(u8 alpha)
{
	int c = 0;

    SetFontSize(APP_FONT_SIZE_SELECTION);
    for (int ind = 0, y_off = 200; menu_options[ind].name; ind++, y_off += APP_LINE_OFFSET)
    {
        if (menu_options[ind].spacer)
            y_off += APP_LINE_OFFSET;

        SetFontColor(APP_FONT_COLOR | alpha, 0);
        DrawString(MENU_ICON_OFF + MENU_TITLE_OFF + 50, y_off, menu_options[ind].name);

		switch (menu_options[ind].type)
		{
			case APP_OPTION_BOOL:
				c = (*menu_options[ind].value == 1) ? opt_on_png_index : opt_off_png_index;
				DrawTexture(&menu_textures[c], OPTION_ITEM_OFF - 29, y_off, 0, menu_textures[c].width, menu_textures[c].height, 0xFFFFFF00 | alpha);
				break;

			case APP_OPTION_CALL:
				DrawTexture(&menu_textures[orbisPadGetConf()->crossButtonOK ? footer_ico_cross_png_index : footer_ico_circle_png_index], OPTION_ITEM_OFF - 29, y_off+2, 0, menu_textures[footer_ico_cross_png_index].width, menu_textures[footer_ico_cross_png_index].height, 0xFFFFFF00 | alpha);
				break;

			case APP_OPTION_LIST:
				SetFontAlign(FONT_ALIGN_CENTER);
				DrawFormatString(OPTION_ITEM_OFF - 18, y_off, "< %s >", menu_options[ind].options[*menu_options[ind].value]);
				SetFontAlign(FONT_ALIGN_LEFT);
				break;

			case APP_OPTION_INC:
				SetFontAlign(FONT_ALIGN_CENTER);
				DrawFormatString(OPTION_ITEM_OFF - 18, y_off, "- %d +", *menu_options[ind].value);
				SetFontAlign(FONT_ALIGN_LEFT);
				break;

			default:
				break;
		}
        
        if (menu_sel == ind)
        {
            DrawTexture(&menu_textures[mark_line_png_index], 0, y_off, 0, SCREEN_WIDTH, menu_textures[mark_line_png_index].height * 2, 0xFFFFFF00 | alpha);
            DrawTextureCenteredX(&menu_textures[mark_arrow_png_index], MENU_ICON_OFF + MENU_TITLE_OFF, y_off, 0, (2 * APP_LINE_OFFSET) / 3, APP_LINE_OFFSET + 2, 0xFFFFFF00 | alpha);
        }
    }
}

void Draw_OptionsMenu_Ani(void)
{
    int ani = 0;
    for (ani = 0; ani < MENU_ANI_MAX; ani++)
    {
        SDL_RenderClear(renderer);
        DrawHeader_Ani(cat_opt_png_index, _("Settings"), NULL, APP_FONT_TITLE_COLOR, 0xffffffff, ani, 12);
        
		u8 icon_a = (u8)(((ani * 2) > 0xFF) ? 0xFF : (ani * 2));
        int _game_a = (int)(icon_a - (MENU_ANI_MAX / 2)) * 2;
        if (_game_a > 0xFF)
            _game_a = 0xFF;
        u8 game_a = (u8)(_game_a < 0 ? 0 : _game_a);
        
        if (game_a > 0)
        	_draw_OptionsMenu(game_a);
        
        SDL_RenderPresent(renderer);
        
        if (game_a == 0xFF)
            return;
    }
}

void Draw_OptionsMenu(void)
{
    DrawHeader(cat_opt_png_index, 0, _("Settings"), NULL, APP_FONT_TITLE_COLOR | 0xFF, 0xffffffff, 0);
    _draw_OptionsMenu(0xFF);
}
