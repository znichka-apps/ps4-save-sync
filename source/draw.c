#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>

#define STBI_ASSERT(x)
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include "types.h"
#include "libfont.h"
#include "menu.h"

#include <dbglogger.h>
#define LOG dbglogger_log

int LoadMenuTexture(const char* path, int idx)
{
	int d, width, height;
	if (!renderer || !menu_textures || !path || idx < 0 || idx >= TOTAL_MENU_TEXTURES)
		return 0;
	if (!stbi_info(path, &width, &height, &d) ||
		width <= 0 || height <= 0 || width > 4096 || height > 4096)
	{
		LOG("Invalid or unavailable texture (%s)", path);
		return 0;
	}

	LOG("Loading '%s'", path);
	if (menu_textures[idx].texture)
		SDL_DestroyTexture(menu_textures[idx].texture);

	menu_textures[idx].size = 0;
	menu_textures[idx].texture = NULL;
	menu_textures[idx].buffer = (uint32_t*) stbi_load(path, &menu_textures[idx].width, &menu_textures[idx].height, &d, STBI_rgb_alpha);

	if (!menu_textures[idx].buffer)
	{
		const char* reason = stbi_failure_reason();
		LOG("Error loading texture (%s): %s", path, reason ? reason : "unknown error");
		return 0;
	}
	if (menu_textures[idx].width != width || menu_textures[idx].height != height)
	{
		LOG("Invalid texture dimensions (%s)", path);
		stbi_image_free(menu_textures[idx].buffer);
		menu_textures[idx].buffer = NULL;
		return 0;
	}

	// GLES2 supports RGBA8888 only
	for (d = 0; d < menu_textures[idx].width * menu_textures[idx].height; d++)
		menu_textures[idx].buffer[d] = ES32(menu_textures[idx].buffer[d]);

	SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(menu_textures[idx].buffer, menu_textures[idx].width, menu_textures[idx].height, 32, 4 * menu_textures[idx].width,
												0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF);
	if (surface)
	{
		menu_textures[idx].texture = SDL_CreateTextureFromSurface(renderer, surface);
		SDL_FreeSurface(surface);
	}
	stbi_image_free(menu_textures[idx].buffer);
	menu_textures[idx].buffer = NULL;
	if (!menu_textures[idx].texture)
	{
		LOG("Error creating texture (%s): %s", path, SDL_GetError());
		return 0;
	}
	menu_textures[idx].size = menu_textures[idx].width * menu_textures[idx].height * 4;
	return 1;
}

void LoadVmcTexture(int width, int height, uint8_t* icon)
{
	if (menu_textures[icon_png_file_index].texture)
		SDL_DestroyTexture(menu_textures[icon_png_file_index].texture);

	menu_textures[icon_png_file_index].width = width;
	menu_textures[icon_png_file_index].height = height;
	menu_textures[icon_png_file_index].size = width * height * 4;
	menu_textures[icon_png_file_index].buffer = NULL;

	SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(icon, width, height, 32, 4 * width,
												0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF);

	menu_textures[icon_png_file_index].texture = SDL_CreateTextureFromSurface(renderer, surface);

	SDL_FreeSurface(surface);
	free(icon);
}

// draw one background color in virtual 2D coordinates
void DrawBackground2D(u32 rgba)
{
	SDL_Rect rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

	SDL_SetRenderDrawColor(renderer, RGBA_R(rgba), RGBA_G(rgba), RGBA_B(rgba), RGBA_A(rgba));
	SDL_RenderFillRect(renderer, &rect);
}

void _drawListBackground(int off, int icon)
{
	switch (icon)
	{
		case cat_db_png_index:
		case cat_usb_png_index:
		case cat_hdd_png_index:
		case cat_opt_png_index:
		case cat_bup_png_index:
		case cat_warning_png_index:
			DrawTexture(&menu_textures[help_png_index], help_png_x, help_png_y, 0, help_png_w, help_png_h, 0xFFFFFF00 | 0xFF);
			break;

		case cat_sav_png_index:
			DrawTexture(&menu_textures[help_png_index], help_png_x, help_png_y, 0, help_png_w, help_png_h, 0xFFFFFF00 | 0xFF);

			if (menu_textures[icon_png_file_index].size)
			{
				DrawTexture(&menu_textures[help_png_index], SCREEN_WIDTH - 404, help_png_y + 4, 0, menu_textures[icon_png_file_index].width + 8, menu_textures[icon_png_file_index].height + 8, 0xFFFFFF00 | 0xFF);
				DrawTexture(&menu_textures[icon_png_file_index], SCREEN_WIDTH - 400, help_png_y + 8, 0, menu_textures[icon_png_file_index].width, menu_textures[icon_png_file_index].height, 0xFFFFFF00 | 0xFF);
			}
			break;

		case cat_cheats_png_index:
			DrawTexture(&menu_textures[help_png_index], off + MENU_ICON_OFF, help_png_y, 0, (SCREEN_WIDTH - 75) - off - MENU_ICON_OFF, help_png_h, 0xFFFFFF00 | 0xFF);
			break;

		case cat_about_png_index:
			break;

		default:
			break;
	}
}

void DrawHeader_Ani(int icon, const char * hdrTitle, const char * headerSubTitle, u32 rgba, u32 bgrgba, int ani, int div)
{
	u8 icon_a = (u8)(((ani * 2) > 0xFF) ? 0xFF : (ani * 2));
	char headerTitle[44];
	snprintf(headerTitle, sizeof(headerTitle), "%.40s%s", hdrTitle, (strlen(hdrTitle) > 40 ? "..." : ""));

	//------------ Backgrounds
	
	//Background
	DrawBackgroundTexture(0, (u8)bgrgba);

	_drawListBackground(0, icon);
	//------------- Menu Bar
/*
	int cnt, cntMax = ((ani * div) > (SCREEN_WIDTH - 75)) ? (SCREEN_WIDTH - 75) : (ani * div);
	for (cnt = MENU_ICON_OFF; cnt < cntMax; cnt++)
		DrawTexture(&menu_textures[header_line_png_index], cnt, 40, 0, menu_textures[header_line_png_index].width, menu_textures[header_line_png_index].height / 2, 0xffffffff);

	DrawTexture(&menu_textures[header_dot_png_index], cnt - 4, 40, 0, menu_textures[header_dot_png_index].width / 2, menu_textures[header_dot_png_index].height / 2, 0xffffff00 | icon_a);
*/

	//header mini icon
	DrawTextureCenteredX(&menu_textures[znichka_icon_png_index], MENU_ICON_OFF - 20, 32, 0, 96, 96, 0xffffff00 | icon_a);

	//header title string
	SetFontColor(rgba | icon_a, 0);
	SetFontSize(APP_FONT_SIZE_TITLE);
	DrawString(MENU_ICON_OFF + 40, 31, headerTitle);

	//header sub title string
	if (headerSubTitle)
	{
		int width = (SCREEN_WIDTH - 75) - (MENU_ICON_OFF + MENU_TITLE_OFF + WidthFromStr(headerTitle)) - 30;
		SetFontSize(APP_FONT_SIZE_SUBTITLE);
		char * tName = strdup(headerSubTitle);
		while (WidthFromStr(tName) > width)
		{
			tName[strlen(tName) - 1] = 0;
		}
		SetFontAlign(FONT_ALIGN_RIGHT);
		DrawString(SCREEN_WIDTH - 75, 35, tName);
		free(tName);
		SetFontAlign(FONT_ALIGN_LEFT);
	}
}

void DrawHeader(int icon, int xOff, const char * hdrTitle, const char * headerSubTitle, u32 rgba, u32 bgrgba, int mode)
{
	char headerTitle[44];
	snprintf(headerTitle, sizeof(headerTitle), "%.40s%s", hdrTitle, (strlen(hdrTitle) > 40 ? "..." : ""));

	//Background
	DrawBackgroundTexture(xOff, (u8)bgrgba);

	_drawListBackground(xOff, icon);
	//------------ Menu Bar
/*
	int cnt = 0;
	for (cnt = xOff + MENU_ICON_OFF; cnt < (SCREEN_WIDTH - 75); cnt++)
		DrawTexture(&menu_textures[header_line_png_index], cnt, 55, 0, menu_textures[header_line_png_index].width, menu_textures[header_line_png_index].height / 2, 0xffffffff);

	DrawTexture(&menu_textures[header_dot_png_index], cnt - 4, 55, 0, menu_textures[header_dot_png_index].width / 2, menu_textures[header_dot_png_index].height / 2, 0xffffffff);
*/

	//header mini icon
	//header title string
	SetFontColor(rgba, 0);
	if (mode)
	{
		DrawTextureCenteredX(&menu_textures[znichka_icon_png_index], xOff + MENU_ICON_OFF - 12, 40, 0, 64, 64, 0xffffffff);
		SetFontSize(APP_FONT_SIZE_SUBTITLE);
		DrawString(xOff + MENU_ICON_OFF + 40, 35, headerTitle);
	}
	else
	{
		DrawTextureCenteredX(&menu_textures[znichka_icon_png_index], xOff + MENU_ICON_OFF - 20, 32, 0, 96, 96, 0xffffffff);
		SetFontSize(APP_FONT_SIZE_TITLE);
		DrawString(xOff + MENU_ICON_OFF + 40, 31, headerTitle);
	}

	//header sub title string
	if (headerSubTitle)
	{
		int width = (SCREEN_WIDTH - 75) - (MENU_ICON_OFF + MENU_TITLE_OFF + WidthFromStr(headerTitle)) - 30;
		SetFontSize(APP_FONT_SIZE_SUBTITLE);
		char * tName = strdup(headerSubTitle);
		while (WidthFromStr(tName) > width)
		{
			tName[strlen(tName) - 1] = 0;
		}
		SetFontAlign(FONT_ALIGN_RIGHT);
		DrawString(SCREEN_WIDTH - 75, 35, tName);
		free(tName);
		SetFontAlign(FONT_ALIGN_LEFT);
	}
}

void DrawBackgroundTexture(int x, u8 alpha)
{
	DrawTexture(&menu_textures[bgimg_jpg_index], x, 0, 0, SCREEN_WIDTH - x, SCREEN_HEIGHT, 0xFFFFFF00 | alpha);
}

void DrawTexture(png_texture* tex, int x, int y, int z, int w, int h, u32 rgba)
{
	if (!renderer || !tex || !tex->texture || w <= 0 || h <= 0)
		return;
	SDL_Rect dest = {
		.x = x,
		.y = y,
		.w = w,
		.h = h,
	};

	SDL_SetTextureAlphaMod(tex->texture, RGBA_A(rgba));
	SDL_RenderCopy(renderer, tex->texture, NULL, &dest);
}

void DrawTextureCentered(png_texture* tex, int x, int y, int z, int w, int h, u32 rgba)
{
	x -= w / 2;
	y -= h / 2;

	DrawTexture(tex, x, y, z, w, h, rgba);
}

void DrawTextureCenteredX(png_texture* tex, int x, int y, int z, int w, int h, u32 rgba)
{
	x -= w / 2;

	DrawTexture(tex, x, y, z, w, h, rgba);
}

void DrawTextureCenteredY(png_texture* tex, int x, int y, int z, int w, int h, u32 rgba)
{
	y -= h / 2;

	DrawTexture(tex, x, y, z, w, h, rgba);
}

void DrawTextureRotated(png_texture* tex, int x, int y, int z, int w, int h, u32 rgba, float angle)
{
	SDL_Rect dest = {
		.x = x - (w / 2),
		.y = y - (h / 2),
		.w = w,
		.h = h,
	};

	SDL_RenderCopyEx(renderer, tex->texture, NULL, &dest, angle, NULL, SDL_FLIP_NONE);
}

static void drawOrbit(int x, int y, int radius)
{
	for (int i = 0; i < 32; i++) {
		float a = (float)i * 6.2831853f / 32.0f;
		float b = (float)(i + 1) * 6.2831853f / 32.0f;
		SDL_RenderDrawLine(renderer, x + (int)(cosf(a) * radius), y + (int)(sinf(a) * radius),
			x + (int)(cosf(b) * radius), y + (int)(sinf(b) * radius));
	}
}

static void drawCardIcon(int index, int x, int y, int active, uint8_t alpha)
{
	SDL_SetRenderDrawColor(renderer, active ? 246 : 126, active ? 211 : 142,
		active ? 92 : 167, alpha);
	switch (index) {
	case 0: /* trophy star */
		for (int i = 0; i < 8; i++) {
			float a = (float)i * 6.2831853f / 8.0f;
			SDL_RenderDrawLine(renderer, x, y, x + (int)(cosf(a) * 35), y + (int)(sinf(a) * 35));
		}
		drawOrbit(x, y, 8);
		break;
	case 1: /* USB branch */
		SDL_RenderDrawLine(renderer, x, y + 32, x, y - 31);
		SDL_RenderDrawLine(renderer, x, y - 11, x - 27, y - 11);
		SDL_RenderDrawLine(renderer, x, y + 8, x + 27, y + 8);
		SDL_RenderDrawLine(renderer, x, y - 31, x - 7, y - 23);
		SDL_RenderDrawLine(renderer, x, y - 31, x + 7, y - 23);
		SDL_Rect usb = {x - 31, y - 15, 8, 8}; SDL_RenderFillRect(renderer, &usb);
		usb.x = x + 23; usb.y = y + 4; SDL_RenderFillRect(renderer, &usb);
		break;
	case 2: /* drive bays */
		for (int i = 0; i < 2; i++) {
			SDL_Rect bay = {x - 35, y - 25 + i * 32, 70, 24};
			SDL_RenderDrawRect(renderer, &bay);
			SDL_Rect light = {x + 20, y - 16 + i * 32, 5, 5}; SDL_RenderFillRect(renderer, &light);
		}
		break;
	case 3: /* online database */
		drawOrbit(x, y, 28); drawOrbit(x, y, 8);
		SDL_RenderDrawLine(renderer, x - 35, y, x + 35, y);
		SDL_RenderDrawLine(renderer, x, y - 35, x, y + 35);
		break;
	case 4: /* cloud and save arrow */
		for (int i = 0; i < 12; i++) {
			float a = 3.1415927f + (float)i * 3.1415927f / 11.0f;
			float b = 3.1415927f + (float)(i + 1) * 3.1415927f / 11.0f;
			SDL_RenderDrawLine(renderer, x - 18 + (int)(cosf(a) * 17), y + 9 + (int)(sinf(a) * 17),
				x - 18 + (int)(cosf(b) * 17), y + 9 + (int)(sinf(b) * 17));
			SDL_RenderDrawLine(renderer, x + 8 + (int)(cosf(a) * 27), y + 1 + (int)(sinf(a) * 27),
				x + 8 + (int)(cosf(b) * 27), y + 1 + (int)(sinf(b) * 27));
		}
		SDL_RenderDrawLine(renderer, x - 35, y + 9, x - 35, y + 24);
		SDL_RenderDrawLine(renderer, x - 35, y + 24, x + 35, y + 24);
		SDL_RenderDrawLine(renderer, x + 35, y + 1, x + 35, y + 24);
		SDL_RenderDrawLine(renderer, x, y - 12, x, y + 9);
		SDL_RenderDrawLine(renderer, x - 10, y, x, y + 10);
		SDL_RenderDrawLine(renderer, x + 10, y, x, y + 10);
		break;
	case 5: /* tools */
		drawOrbit(x, y, 25);
		SDL_RenderDrawLine(renderer, x - 35, y, x + 35, y);
		SDL_RenderDrawLine(renderer, x, y - 35, x, y + 35);
		drawOrbit(x, y, 8);
		break;
	case 6: /* settings sliders */
		for (int i = 0; i < 3; i++) {
			int yy = y - 25 + i * 25;
			SDL_RenderDrawLine(renderer, x - 34, yy, x + 34, yy);
			SDL_Rect knob = {x - 19 + i * 15, yy - 6, 12, 12}; SDL_RenderFillRect(renderer, &knob);
		}
		break;
	default: /* about */
		drawOrbit(x, y, 33);
		SDL_Rect dot = {x - 3, y - 19, 6, 6}; SDL_RenderFillRect(renderer, &dot);
		SDL_RenderDrawLine(renderer, x, y - 5, x, y + 20);
		break;
	}
}

static void drawMainCards(uint8_t alpha)
{
	static const char *labels[8] = {"Trophies", "USB Saves", "HDD Saves", "Online Database",
		"Google Drive", "User Tools", "Settings", "About"};
	static const char *details[8] = {"Trophy saves", "USB storage", "Internal storage", "Browse online saves",
		"Cloud backup / restore", "Local save tools", "App preferences", "App information"};
	SetFontAlign(FONT_ALIGN_LEFT);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	for (int i = 0; i < 8; i++) {
		int x = 144 + (i % 4) * 413;
		int y = 440 + (i / 4) * 208;
		int active = (menu_sel == i);
		SDL_Rect card = {x, y, 380, 180};
		SDL_SetRenderDrawColor(renderer, active ? 28 : 12,
			active ? 34 : 19, active ? 70 : 38,
			active ? alpha * 235 / 255 : alpha * 220 / 255);
		SDL_RenderFillRect(renderer, &card);
		if (active) {
			SDL_SetRenderDrawColor(renderer, 246, 211, 92, alpha);
			SDL_RenderDrawRect(renderer, &card);
		}
		drawCardIcon(i, x + 56, y + 79, active, alpha);
		SetFontColor((active ? 0xF6D35C00 : APP_FONT_COLOR) | alpha, 0);
		SetFontSize(i == 3 ? 34 : 40, i == 3 ? 38 : 44);
		DrawString(x + 111, y + 50, i == 3 && apollo_config.online_opt ? "FTP Server" : labels[i]);
		SetFontColor(0xC8D5ED00 | alpha, 0);
		SetFontSize(30, 34);
		DrawString(x + 24, y + 132, details[i]);
	}
	SetFontAlign(FONT_ALIGN_SCREEN_CENTER);
	SetFontColor(APP_FONT_COLOR | alpha, 0);
	SetFontSize(36, 40);
	DrawString(0, 921, "Left / Right  Navigate       \x10 Open       \x13 Exit");
	SetFontAlign(FONT_ALIGN_LEFT);
}

static void drawStartupIdentity(u8 alpha)
{
	if (menu_textures && menu_textures[znichka_icon_png_index].texture)
		DrawTexture(&menu_textures[znichka_icon_png_index], 64, 56, 0,
			76, 76, 0xFFFFFF00 | alpha);
	else
	{
		SetFontAlign(FONT_ALIGN_LEFT);
		SetFontColor(APP_FONT_TITLE_COLOR | alpha, 0);
		SetFontSize(48, 54);
		DrawString(64, 58, "Znichka");
	}
	if (menu_textures && menu_textures[cloud_save_logo_png_index].texture)
	{
		DrawTexture(&menu_textures[cloud_save_logo_png_index],
			(SCREEN_WIDTH - 900) / 2, (SCREEN_HEIGHT - 550) / 2, 0,
			900, 550, 0xFFFFFF00 | alpha);
	}
	else
	{
		SetFontAlign(FONT_ALIGN_SCREEN_CENTER);
		SetFontColor(APP_FONT_TITLE_COLOR | alpha, 0);
		SetFontSize(72, 76);
		DrawString(0, SCREEN_HEIGHT / 2 - 80, "PS4 Cloud Save");
		SetFontColor(0xE9EEF900 | alpha, 0);
		SetFontSize(38, 44);
		DrawString(0, SCREEN_HEIGHT / 2 + 10, "by Znichka");
		SetFontAlign(FONT_ALIGN_LEFT);
	}
}

void drawSplashLogo(void)
{
	SDL_RenderClear(renderer);
	DrawBackground2D(0x071026FF);
	drawStartupIdentity(0xFF);
	SDL_RenderPresent(renderer);
}

void drawEndLogo(void)
{
	drawSplashLogo();
}

static void _draw_MainMenu(uint8_t alpha)
{
	DrawBackgroundTexture(0, 0xFF);
	DrawTexture(&menu_textures[znichka_icon_png_index], 146, 82, 0,
		104, 104, 0xFFFFFF00 | alpha);
	SetFontAlign(FONT_ALIGN_LEFT);
	SetFontColor(APP_FONT_TITLE_COLOR | alpha, 0);
	SetFontSize(72, 76);
	DrawString(276, 77, "PS4 Cloud Save");
	SetFontColor(0xE9EEF900 | alpha, 0);
	SetFontSize(38, 44);
	DrawString(282, 165, "by Znichka");
	SetFontSize(43, 48);
	DrawString(146, 304, "Back up and restore PS4 saves with Google Drive");
	drawMainCards(alpha);
}

void Draw_MainMenu_Ani(void)
{
	int max = MENU_ANI_MAX, ani = 0;
	for (ani = 0; ani < max; ani++)
	{
		SDL_RenderClear(renderer);
		DrawBackground2D(0x070A12FF);
		
		//------------ Backgrounds
		u8 bg_a = (u8)(ani * 2);
		if (bg_a < 0x20)
			bg_a = 0x20;
		int logo_a_t = ((ani < 0x30) ? 0 : ((ani - 0x20)*3));
		if (logo_a_t > 0xFF)
			logo_a_t = 0xFF;
		u8 logo_a = (u8)logo_a_t;
		
		//Background
		DrawBackgroundTexture(0, bg_a);
		
		if (logo_a) {
			DrawTexture(&menu_textures[znichka_icon_png_index], 146, 82, 0,
				104, 104, 0xFFFFFF00 | logo_a);
			SetFontAlign(FONT_ALIGN_LEFT);
			SetFontColor(APP_FONT_TITLE_COLOR | logo_a, 0);
			SetFontSize(72, 76);
			DrawString(276, 77, "PS4 Cloud Save");
			SetFontColor(0xE9EEF900 | logo_a, 0);
			SetFontSize(38, 44);
			DrawString(282, 165, "by Znichka");
		}

		SDL_RenderPresent(renderer);
	}
	
	max = MENU_ANI_MAX / 2;
	int rate = (0x100 / max);
	for (ani = 0; ani < max; ani++)
	{
		SDL_RenderClear(renderer);
		DrawBackground2D(0x070A12FF);
		
		u8 icon_a = (u8)(((ani * rate) > 0xFF) ? 0xFF : (ani * rate));
		
		_draw_MainMenu(icon_a);
		
		SDL_RenderPresent(renderer);

		if (icon_a == 32)
			break;
	}
}

void Draw_MainMenu(void)
{
	_draw_MainMenu(0xFF);
}
