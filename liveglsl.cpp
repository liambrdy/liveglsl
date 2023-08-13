#include <cstdio>
#include <vector>
#include <cassert>
#include <algorithm>
#include <string>

#include <raylib.h>
#include <rlgl.h>

#include "font.h"

static const char *textFrag = R"(#version 440 core

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

out vec4 finalColor;

void main()
{
	// Texel color fetching from texture sampler
	// NOTE: Calculate alpha using signed distance field (SDF)
	float distanceFromOutline = texture(texture0, fragTexCoord).a - 0.5;
	float distanceChangePerFragment = length(vec2(dFdx(distanceFromOutline), dFdy(distanceFromOutline)));
	float alpha = smoothstep(-distanceChangePerFragment, distanceChangePerFragment, distanceFromOutline);

	// Calculate final fragment color
	finalColor = vec4(fragColor.rgb, fragColor.a*alpha);
}
)";

static const char *initialCode = R"(#version 440 core

out vec4 outColor;
in vec2 fragTexCoord;

uniform vec2 resolution;
uniform float time;

void main() {
	vec2 uv = gl_FragCoord.xy * 2.0 - resolution;
	uv /= min(resolution.x, resolution.y);

	float d = length(uv);
	d = abs(d);
	d = sin(d*8.0 + time);
	d = 0.02 / d;

	outColor = vec4(d, d, d, 1.0);
}
)";

struct Line {
	std::vector<char> items;
};

struct Editor {
	std::vector<Line> lines;
	int cursorLine, cursorCol;
	int topLine;
	int linesOnScreen;
	int currentHighCol;
};

struct Error {
	int line, col;
	std::string text;
};

std::vector<Error> errors;

void ImportStringToEditor(Editor *e, const char *text) {
	int lineBegin = 0;
	Line line = {};
	for (int i = 0; i < strlen(text); i++) {
		if (text[i] == '\n') {
			e->lines.push_back(line);
			line.items.clear();
		}
		else if (text[i] == '\t') {
			for (int j = 0; j < 5; j++) line.items.push_back(' ');
		}
		else {
			line.items.push_back(text[i]);
		}
	}
}

char *EditorToString(Editor *e) {
	int length = 0;
	for (const auto &l : e->lines) {
		length += l.items.size();
		length++;
	}

	char *string = (char *)malloc(length + 1);
	assert(string);
	int stringIndex = 0;
	for (const auto &l : e->lines) {
		assert(stringIndex < length);
		for (const auto &c : l.items) {
			string[stringIndex++] = c;
		}
		string[stringIndex++] = '\n';
	}

	string[stringIndex] = '\0';
	return string;
}

int main() {
	int width = 1280*2, height = 720*2;
	int oldWidth = 0, oldHeight = 0;
	InitWindow(width, height, "liveglsl");

	SetWindowState(FLAG_WINDOW_RESIZABLE);

	SetTargetFPS(60);

	const char *saveName = "shader.frag";

	Shader liveShader = {};

	Editor editor = {};
	if (FileExists(saveName)) {
		char *code = LoadFileText(saveName);
		ImportStringToEditor(&editor, code);
		liveShader = LoadShaderFromMemory(nullptr, code);
		UnloadFileText(code);
	} else {
		ImportStringToEditor(&editor, initialCode);
		liveShader = LoadShaderFromMemory(nullptr, initialCode);
	}

	int fontSize = 50;
	int editorFontSize = fontSize;

	//unsigned int fileSize = 0;
	//unsigned char *fileData = LoadFileData("..\\..\\..\\Fonts\\AnonymousPro-Regular.ttf", &fileSize);

	Font font = { 0 };
	font.baseSize = fontSize;
	font.glyphCount = 95;
	//font.glyphs = LoadFontData(fileData, fileSize, fontSize, 0, 0, FONT_SDF);
	font.glyphs = LoadFontData((const unsigned char *)fontData, fontDataSize, fontSize, 0, 0, FONT_SDF);
	Image atlas = GenImageFontAtlas(font.glyphs, &font.recs, 95, fontSize, 5, 0);
	font.texture = LoadTextureFromImage(atlas);
	UnloadImage(atlas);
	//UnloadFileData(fileData);

	Shader textShader = LoadShaderFromMemory(nullptr, textFrag);
	SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

	int charLength = MeasureText("a", fontSize);

	int resolutionLoc = GetShaderLocation(liveShader, "resolution");
	int timeLoc = GetShaderLocation(liveShader, "time");

	Vector2 resolution = { width, height };
	editor.linesOnScreen = height / editorFontSize;

	SetTraceLogCallback([](int logLevel, const char *text, va_list args)
		{
			std::string s(text);
			s.push_back('\n');
			vprintf(s.c_str(), args);
			if (logLevel == LOG_WARNING) {
				std::string str(text);
				if (str.find("Compile error: %s") != std::string::npos) {
					//va_start(args, text);
					unsigned int s = va_arg(args, unsigned int);
					char *log = va_arg(args, char *);
					int line = log[9] - '0';
					int furthest = 10;
					for (int i = 1; log[9 + i] != ':'; i++) {
						line *= 10;
						line += log[9 + i] - '0';
						furthest++;
					}
					line -= 2;
					bool exists = false;
					for (const auto &e : errors) {
						if (e.line == line) {
							exists = true;
							break;
						}
					}
					if (!exists) {
						printf("Error on line %d\n", line);
						Error e = {};
						e.line = line;
						e.col = 0;
						e.text = std::string(log + furthest);
						errors.push_back(e);
					}
				}
				else {
					//vprintf(text, args);
				}
			}
		});

	bool showEditor = true;
	bool error = false;
	int furthestX = 0;
	int tmpFurthestX = 0;
	float backgroundAlpha = 0.6f;
	while (!WindowShouldClose()) {
		if (IsWindowResized()) {
			width = GetRenderWidth();
			height = GetRenderHeight();
			resolution.x = width;
			resolution.y = height;
		}

		if (IsKeyPressed(KEY_F1)) {
			int maxWidth = GetMonitorWidth(0);
			int maxHeight = GetMonitorHeight(0);
			int currentWidth = GetRenderWidth();
			int currentHeight = GetRenderHeight();
			if (maxWidth == currentWidth && maxHeight == currentHeight) {
				width = oldWidth;
				height = oldHeight;
				resolution.x = width;
				resolution.y = height;

				ToggleFullscreen();
				SetWindowSize(width, height);
			}
			else {
				oldWidth = width;
				oldHeight = height;
				width = maxWidth;
				height = maxHeight;
				resolution.x = width;
				resolution.y = height;

				SetWindowSize(width, height);
				ToggleFullscreen();
			}
		}

		if (IsKeyPressed(KEY_LEFT) && editor.cursorCol > 0) {
			editor.cursorCol--;
			editor.currentHighCol = editor.cursorCol;
		}
		if (IsKeyPressed(KEY_RIGHT) && editor.cursorCol < editor.lines[editor.cursorLine].items.size()) {
			editor.cursorCol++;
			editor.currentHighCol = editor.cursorCol;
		}
		if (IsKeyPressed(KEY_UP) && editor.cursorLine > 0) {
			if (IsKeyDown(KEY_RIGHT_CONTROL)) {
				backgroundAlpha += 0.1f;
				backgroundAlpha = std::min(1.0f, backgroundAlpha);
			}
			else {
				editor.cursorLine--;
				editor.cursorCol = std::min((int)editor.lines[editor.cursorLine].items.size(), std::max(editor.cursorCol, editor.currentHighCol));
				if (editor.cursorLine < editor.topLine) {
					editor.topLine = editor.cursorLine;
				}
			}
		}
		if (IsKeyPressed(KEY_DOWN) && editor.cursorLine + 1 < editor.lines.size()) {
			if (IsKeyDown(KEY_RIGHT_CONTROL)) {
				backgroundAlpha -= 0.1f;
				backgroundAlpha = std::max(0.0f, backgroundAlpha);
			}
			else {
				editor.cursorLine++;
				editor.cursorCol = std::min((int)editor.lines[editor.cursorLine].items.size(), std::max(editor.cursorCol, editor.currentHighCol));
				if (editor.cursorLine - editor.topLine > editor.linesOnScreen) {
					editor.topLine++;
				}
			}
		}
		if (IsKeyPressed(KEY_ENTER)) {
			if (IsKeyDown(KEY_LEFT_ALT)) {
				errors.clear();
				char *shaderCode = EditorToString(&editor);
				Shader tmpShader = LoadShaderFromMemory(nullptr, shaderCode);
				if (IsShaderReady(tmpShader) && tmpShader.id != rlGetShaderIdDefault()) {
					liveShader = tmpShader;
					resolutionLoc = GetShaderLocation(liveShader, "resolution");
					timeLoc = GetShaderLocation(liveShader, "time");
				}
				free(shaderCode);
			} else {
				Line l = {};
				Line &last = editor.lines[editor.cursorLine];
				if (editor.cursorCol < last.items.size()) {
					l.items.insert(l.items.begin(), last.items.begin() + editor.cursorCol, last.items.end());
					last.items.erase(last.items.begin() + editor.cursorCol, last.items.end());
				}
				editor.lines.insert(editor.lines.begin() + editor.cursorLine + 1, l);
				editor.cursorLine++;
				editor.cursorCol = 0;
			}
		}
		if (IsKeyPressed(KEY_BACKSPACE)) {
			auto &l = editor.lines[editor.cursorLine];
			if (editor.cursorCol > 0) {
				l.items.erase(l.items.begin() + editor.cursorCol - 1);
				editor.cursorCol--;
			}
			else if (editor.cursorCol == 0 && editor.cursorLine > 0) {
				Line &l = editor.lines[editor.cursorLine];
				Line &last = editor.lines[editor.cursorLine - 1];
				int lastSize = last.items.size();
				if (!l.items.empty()) {
					last.items.insert(last.items.end(), l.items.begin(), l.items.end());
				}
				editor.lines.erase(editor.lines.begin() + editor.cursorLine);
				editor.cursorLine--;
				editor.cursorCol = lastSize;
			}
		}
		if (IsKeyPressed(KEY_TAB)) {
			if (IsKeyDown(KEY_LEFT_CONTROL)) {
				showEditor = !showEditor;
			} else {
				auto &l = editor.lines[editor.cursorLine];
				l.items.insert(l.items.begin() + editor.cursorCol, 5, ' ');
				editor.cursorCol += 5;
			}
		}
		if (IsKeyPressed(KEY_S) && IsKeyDown(KEY_LEFT_CONTROL)) {
			char *shaderCode = EditorToString(&editor);
			SaveFileData(saveName, (void *)shaderCode, strlen(shaderCode));
			free(shaderCode);
		}
		if (IsKeyPressed(KEY_MINUS) && IsKeyDown(KEY_LEFT_CONTROL)) {
			editorFontSize -= 5;
			tmpFurthestX = furthestX;
			furthestX = 0;
			editor.linesOnScreen = height / editorFontSize;
		}
		if (IsKeyPressed(KEY_EQUAL) && IsKeyDown(KEY_LEFT_CONTROL)) {
			editorFontSize += 5;
			editor.linesOnScreen = height / editorFontSize;
		}
		if (IsKeyPressed(KEY_E) && IsKeyDown(KEY_LEFT_CONTROL)) {
			editor.cursorCol = editor.lines[editor.cursorLine].items.size();
		}
		if (IsKeyPressed(KEY_A) && IsKeyDown(KEY_LEFT_CONTROL)) {
			Line &l = editor.lines[editor.cursorLine];
			if (editor.cursorCol == 0) {
				int tab = 0;
				while (l.items[tab] == ' ') {
					tab++;
				}
				editor.cursorCol = tab;
			} else {
				editor.cursorCol = 0;
			}
		}
		char chr = (char)GetCharPressed();
		if (chr != 0) {
			auto &line = editor.lines[editor.cursorLine];
			line.items.insert(line.items.begin() + editor.cursorCol, chr);
			editor.cursorCol++;
		}

		if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
			int x = GetMouseX();
			int y = GetMouseY();

			int line = editor.topLine + (int)floorf((float)y / editorFontSize);
			line = std::min(line, (int)editor.lines.size() - 1);
			int col = 0;

			Line &l = editor.lines[line];
			int currentX = 0;
			for (const auto &c : l.items) {
				GlyphInfo info = GetGlyphInfo(font, (int)c);
				if (currentX + info.advanceX > x) {
					break;
				}
				float scaleFactor = (float)editorFontSize / font.baseSize;
				currentX += info.advanceX * scaleFactor;
				col++;
			}

			editor.cursorCol = col;
			editor.cursorLine = line;
		}

		Vector2 mouseWheel = GetMouseWheelMoveV();
		const float EPS = 0.0001f;
		if (mouseWheel.y < -EPS || EPS < mouseWheel.y) {
			editor.topLine += (int)mouseWheel.y;
			editor.topLine = std::max(0, std::min((int)editor.lines.size(), editor.topLine));
		}

		ClearBackground(DARKGRAY);

		float time = GetTime();
		SetShaderValue(liveShader, resolutionLoc, (const void *)&resolution, SHADER_UNIFORM_VEC2);
		SetShaderValue(liveShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);

		BeginDrawing();

		BeginShaderMode(liveShader);
		DrawRectangle(0, 0, width, height, WHITE);
		EndShaderMode();

		if (showEditor) {
			if (furthestX == 0)
				DrawRectangle(0, 0, tmpFurthestX, height, { 50, 50, 50, (unsigned char)(backgroundAlpha*255.0f) });
			else
				DrawRectangle(0, 0, furthestX, height, { 50, 50, 50, (unsigned char)(backgroundAlpha * 255.0f) });
			tmpFurthestX = furthestX;
			
			if (!errors.empty()) {
				for (const auto &error : errors) {
					Line &errorL = editor.lines[error.line];
					int xLength = 0;
					for (const auto &c : errorL.items) {
						GlyphInfo info = GetGlyphInfo(font, (int)c);
						float scaleFactor = (float)editorFontSize / font.baseSize;
						xLength += info.advanceX * scaleFactor;
					}
					DrawRectangle(0, error.line * editorFontSize, xLength, editorFontSize, { 255, 0, 0, 150 });
					DrawTextEx(font, error.text.c_str(), { (float)xLength + 5, (float)error.line * editorFontSize }, editorFontSize, 0, RED);
				}
			}

			BeginShaderMode(textShader);
			// DrawTextEx(font, initialCode, {0, 0}, textSize, 0, WHITE);

			for (int i = editor.topLine; i < editor.lines.size(); i++) {
				Line l = editor.lines[i];
				int x = 0;
				if (!l.items.empty()) {
					for (int j = 0; j < l.items.size(); j++) {
						char c = l.items[j];
						GlyphInfo info = GetGlyphInfo(font, (int)c);
						DrawTextCodepoint(font, (int)c, { (float)x, (float)(i - editor.topLine) * editorFontSize }, editorFontSize, WHITE);
						float scaleFactor = (float)editorFontSize / font.baseSize;
						x += info.advanceX * scaleFactor;
						if (x > furthestX) furthestX = x;
					}
				}
				//DrawTextCodepoints(font, (const int *)l.items.data(), l.items.size(), { 0, (float)i * textSize }, textSize, 0, WHITE);
			}

			int fps = GetFPS();
			const char *str = TextFormat("FPS: %d", fps);
			Vector2 fpsMeasure = MeasureTextEx(font, str, editorFontSize, 0);
			DrawRectangle(width - fpsMeasure.x, 0, fpsMeasure.x, editorFontSize, { 50, 50, 50, 150 });
			DrawTextEx(font, str, { width - fpsMeasure.x, 0 }, editorFontSize, 0, WHITE);

			EndShaderMode();
			int cursorPos = 0;
			Line l = editor.lines[editor.cursorLine];
			for (int i = 0; i < editor.cursorCol; i++) {
				char c = l.items[i];
				GlyphInfo info = GetGlyphInfo(font, (int)c);
				float scaleFactor = (float)editorFontSize / font.baseSize;
				cursorPos += info.advanceX * scaleFactor;
			}
			Color cursorColor = WHITE;
			DrawRectangle(cursorPos, (editor.cursorLine - editor.topLine) * editorFontSize, editorFontSize / 4, editorFontSize, cursorColor);
		}
		
		//DrawTexture(font.texture, width - font.texture.width, 0, WHITE);

		EndDrawing();
	}

	CloseWindow();

	return 0;
}