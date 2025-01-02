#include <cstdio>
#include <vector>
#include <cassert>
#include <algorithm>
#include <string>

#include <raylib.h>
#include <rlgl.h>

#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

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

struct Location {
	int line;
	int col;
};

struct Editor {
	std::vector<Line> lines;
	Location cursor;

	int topLine;
	int linesOnScreen;
	int currentHighCol;

	Location selectLoc;
	bool selecting;

	bool fileSelect;
	std::vector<std::string> shaderFileNames;
	int fileSelectLine;

	bool nameSelect;
	std::string nameEditor;
	int nameEditorCol;

	std::vector<Location> keywords;
};

struct Error {
	int line, col;
	std::string text;
};

std::vector<Error> errors;

void ImportStringToEditor(Editor *e, const char *text) {
	e->lines.clear();
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

void ParseWords(Editor *editor) {
	const std::string KEYWORDS[] = {
		"void", "int", "vec2", "vec3", "vec4",
		"uniform", "float", "gl_FragCoord", "length",
		"abs", "sin", "min", "max", "cos", "mat4", "mat3"
	};

	editor->keywords.clear();

	for (int i = 0; i < editor->lines.size(); i++) {
		auto &l = editor->lines[i];

		std::vector<std::pair<int, std::string>> words;
		std::string currentWord;
		int firstCol = INT_MAX;
		for (int j = 0; j < l.items.size(); j++) {
			char c = l.items[j];
			if (isalnum(c) || c == '_') {
				currentWord.push_back(c);

				if (firstCol == INT_MAX) {
					firstCol = j;
				}
			} else if ((c == ' ' || c == '(' || c == '.') && firstCol != INT_MAX) {
				words.push_back({ firstCol, currentWord });
				currentWord.clear();
				firstCol = INT_MAX;
			}
		}

		if (firstCol != INT_MAX) {
			words.push_back({ firstCol, currentWord });
		}

		for (const auto &[c, w] : words) {
			for (const auto &k : KEYWORDS) {
				if (w.compare(k) == 0) {
					editor->keywords.push_back({ i, c });
				}
			}
		}
	}
}

int main() {
	int width = 1280*2, height = 720*2;
	int oldWidth = 0, oldHeight = 0;
	InitWindow(width, height, "liveglsl");

	SetWindowState(FLAG_WINDOW_RESIZABLE);
	SetExitKey(0);

	SetTargetFPS(60);

	if (!DirectoryExists("shaders")) {
		system("mkdir shaders");
	}

	assert(ChangeDirectory("shaders"));

	std::string saveName;

	Shader liveShader = {};

	Editor editor = {};
	ImportStringToEditor(&editor, initialCode);
	liveShader = LoadShaderFromMemory(nullptr, initialCode);

	ParseWords(&editor);

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
						e.line = std::max(0, line);
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

	int formatLen = strlen("Save file: ");

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

		if (IsKeyDown(KEY_LEFT_CONTROL)) {
			if (IsKeyPressed(KEY_S)) {
				if (saveName.empty() || IsKeyDown(KEY_LEFT_SHIFT)) {
					editor.nameSelect = true;
				} else {
					char *shaderCode = EditorToString(&editor);
					SaveFileData(saveName.c_str(), (void *)shaderCode, strlen(shaderCode));
					free(shaderCode);
				}
			}
			if (IsKeyPressed(KEY_MINUS)) {
				editorFontSize -= 5;
				tmpFurthestX = furthestX;
				furthestX = 0;
				editor.linesOnScreen = height / editorFontSize;
			}
			if (IsKeyPressed(KEY_EQUAL)) {
				editorFontSize += 5;
				editor.linesOnScreen = height / editorFontSize;
			}
			if (IsKeyPressed(KEY_E)) {
				editor.cursor.col = editor.lines[editor.cursor.line].items.size();
			}
			if (IsKeyPressed(KEY_A)) {
				Line &l = editor.lines[editor.cursor.line];
				if (editor.cursor.col == 0) {
					int tab = 0;
					while (l.items[tab] == ' ') {
						tab++;
					}
					editor.cursor.col = tab;
				} else {
					editor.cursor.col = 0;
				}
			}
			if (IsKeyPressed(KEY_O)) {
				if (editor.fileSelect) {
					editor.fileSelect = false;
				} else {
					editor.fileSelect = true;
					FilePathList list = LoadDirectoryFilesEx(".", ".frag", false);
					editor.shaderFileNames.clear();
					for (int i = 0; i < list.count; i++) {
						editor.shaderFileNames.push_back(list.paths[i]);
					}
				}
			}
			if (IsKeyPressed(KEY_N)) {
				editor = Editor{};
				saveName = "";
				errors.clear();
				ImportStringToEditor(&editor, initialCode);
				liveShader = LoadShaderFromMemory(nullptr, initialCode);
			}

			if (IsKeyPressed(KEY_LEFT)) {
				auto &l = editor.lines[editor.cursor.line];
				for (int i = editor.cursor.col; i >= 0; i--) {
					if (l.items[i] == ' ') {
						break;
					}
					editor.cursor.col = i;
				}
			}

			if (IsKeyPressed(KEY_RIGHT)) {
				auto &l = editor.lines[editor.cursor.line];
				for (int i = editor.cursor.col; i < l.items.size(); i++) {
					if (l.items[i] == ' ') {
						break;
					}
					editor.cursor.col = i;
				}
			}
		}

		if (editor.fileSelect) {
			if (IsKeyPressed(KEY_ESCAPE)) {
				editor.fileSelect = false;
			}

			if (IsKeyPressed(KEY_DOWN) && editor.fileSelectLine + 1 < editor.shaderFileNames.size()) {
				editor.fileSelectLine++;
			}
			if (IsKeyPressed(KEY_UP) && editor.fileSelectLine > 0) {
				editor.fileSelectLine--;
			}

			if (IsKeyPressed(KEY_ENTER)) {
				char *code = LoadFileText(editor.shaderFileNames[editor.fileSelectLine].c_str());
				ImportStringToEditor(&editor, code);
				liveShader = LoadShaderFromMemory(nullptr, code);
				UnloadFileText(code);

				saveName = editor.shaderFileNames[editor.fileSelectLine];

				editor.cursor.col = 0;
				editor.cursor.line = 0;
				editor.fileSelectLine = 0;
				editor.fileSelect = false;
				editor.nameEditor.clear();
			}
		}
		else if (editor.nameSelect) {
			if (IsKeyPressed(KEY_ENTER)) {
				saveName = editor.nameEditor;

				char *shaderCode = EditorToString(&editor);
				SaveFileData(saveName.c_str(), (void *)shaderCode, strlen(shaderCode));
				free(shaderCode);

				editor.nameEditorCol = 0;
				editor.nameSelect = false;
			}

			if (IsKeyPressed(KEY_ESCAPE)) {
				editor.nameSelect = false;
			}

			if (IsKeyPressed(KEY_RIGHT) && editor.nameEditorCol < editor.nameEditor.size()) {
				editor.nameEditorCol++;
			}

			if (IsKeyPressed(KEY_LEFT) && editor.nameEditorCol > 0) {
				editor.nameEditorCol--;
			}

			if (IsKeyPressed(KEY_BACKSPACE) && editor.nameEditorCol > 0) {
				editor.nameEditor.erase(editor.nameEditor.begin() + editor.nameEditorCol - 1);
				editor.nameEditorCol--;
			}
		} else {
			if (IsKeyPressed(KEY_LEFT) && editor.cursor.col > 0) {
				if (IsKeyDown(KEY_LEFT_SHIFT) && !editor.selecting) {
					editor.selecting = true;
					editor.selectLoc.col = editor.cursor.col;
					editor.selectLoc.line = editor.cursor.line;
				} else {
					editor.selecting = false;
				}

				editor.cursor.col--;
				editor.currentHighCol = editor.cursor.col;
			}
			if (IsKeyPressed(KEY_RIGHT) && editor.cursor.col < editor.lines[editor.cursor.line].items.size()) {
				if (IsKeyDown(KEY_LEFT_SHIFT) && !editor.selecting) {
					editor.selecting = true;
					editor.selectLoc.col = editor.cursor.col;
					editor.selectLoc.line = editor.cursor.line;
				} else {
					editor.selecting = false;
				}

				editor.cursor.col++;
				editor.currentHighCol = editor.cursor.col;
			}
			if (IsKeyPressed(KEY_UP)) {
				if (editor.cursor.line > 0) {
					if (IsKeyDown(KEY_RIGHT_CONTROL)) {
						backgroundAlpha += 0.1f;
						backgroundAlpha = std::min(1.0f, backgroundAlpha);
					} else {
						if (IsKeyDown(KEY_LEFT_SHIFT) && !editor.selecting) {
							editor.selecting = true;
							editor.selectLoc.col = editor.cursor.col;
							editor.selectLoc.line = editor.cursor.line;
						} else {
							editor.selecting = false;
						}

						editor.cursor.line--;
						editor.cursor.col = std::min((int)editor.lines[editor.cursor.line].items.size(), std::max(editor.cursor.col, editor.currentHighCol));
						if (editor.cursor.line < editor.topLine) {
							editor.topLine = editor.cursor.line;
						}
					}
				} else {
					editor.cursor.col = 0;
					editor.currentHighCol = 0;
				}
			}
			if (IsKeyPressed(KEY_DOWN) && editor.cursor.line + 1 < editor.lines.size()) {
				if (IsKeyDown(KEY_RIGHT_CONTROL)) {
					backgroundAlpha -= 0.1f;
					backgroundAlpha = std::max(0.0f, backgroundAlpha);
				} else {
					if (IsKeyDown(KEY_LEFT_SHIFT) && !editor.selecting) {
						editor.selecting = true;
						editor.selectLoc.col = editor.cursor.col;
						editor.selectLoc.line = editor.cursor.line;
					} else {
						editor.selecting = false;
					}

					editor.cursor.line++;
					editor.cursor.col = std::min((int)editor.lines[editor.cursor.line].items.size(), std::max(editor.cursor.col, editor.currentHighCol));
					if (editor.cursor.line - editor.topLine > editor.linesOnScreen) {
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
					Line &last = editor.lines[editor.cursor.line];
					if (editor.cursor.col < last.items.size()) {
						l.items.insert(l.items.begin(), last.items.begin() + editor.cursor.col, last.items.end());
						last.items.erase(last.items.begin() + editor.cursor.col, last.items.end());
					}
					editor.lines.insert(editor.lines.begin() + editor.cursor.line + 1, l);
					editor.cursor.line++;
					editor.cursor.col = 0;
				}
			}
			if (IsKeyPressed(KEY_BACKSPACE)) {
				auto &l = editor.lines[editor.cursor.line];
				if (editor.cursor.col > 0) {
					l.items.erase(l.items.begin() + editor.cursor.col - 1);
					editor.cursor.col--;
				} else if (editor.cursor.col == 0 && editor.cursor.line > 0) {
					Line &l = editor.lines[editor.cursor.line];
					Line &last = editor.lines[editor.cursor.line - 1];
					int lastSize = last.items.size();
					if (!l.items.empty()) {
						last.items.insert(last.items.end(), l.items.begin(), l.items.end());
					}
					editor.lines.erase(editor.lines.begin() + editor.cursor.line);
					editor.cursor.line--;
					editor.cursor.col = lastSize;
				}
				ParseWords(&editor);
			}
			if (IsKeyPressed(KEY_TAB)) {
				if (IsKeyDown(KEY_LEFT_CONTROL)) {
					showEditor = !showEditor;
				} else {
					auto &l = editor.lines[editor.cursor.line];
					l.items.insert(l.items.begin() + editor.cursor.col, 5, ' ');
					editor.cursor.col += 5;
				}
			}
		}

		char chr = (char)GetCharPressed();
		if (chr != 0) {
			if (editor.nameSelect) {
				editor.nameEditor.insert(editor.nameEditor.begin() + editor.nameEditorCol, chr);
				editor.nameEditorCol++;
			} else if (!editor.fileSelect) {
				auto &line = editor.lines[editor.cursor.line];
				line.items.insert(line.items.begin() + editor.cursor.col, chr);
				editor.cursor.col++;
				ParseWords(&editor);
			}
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

			editor.cursor.col = col;
			editor.cursor.line = line;
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
			bool highlighting = false;
			// DrawTextEx(font, initialCode, {0, 0}, textSize, 0, WHITE);
			for (int i = editor.topLine; i < editor.lines.size(); i++) {
				Line l = editor.lines[i];
				int x = 0;
				if (!l.items.empty()) {
					for (int j = 0; j < l.items.size(); j++) {
						Location currentLocation = { i, j };
						auto loc = std::find_if(editor.keywords.begin(), editor.keywords.end(), [&cl = currentLocation](const Location &l) {
							return l.line == cl.line && l.col == cl.col;
						});
						if (loc != editor.keywords.end()) {
							highlighting = true;
						}

						char c = l.items[j];
						if (c == ' ' || c == '(' || c == '.') highlighting = false;
						GlyphInfo info = GetGlyphInfo(font, (int)c);
						DrawTextCodepoint(font, (int)c, { (float)x, (float)(i - editor.topLine) * editorFontSize }, editorFontSize, highlighting ? YELLOW : WHITE);
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

			if (editor.fileSelect) {
				int initialX = width / 2;
				int initialY = height / 2;
				for (int i = 0; i < editor.shaderFileNames.size(); i++) {
					DrawTextEx(font, editor.shaderFileNames[i].c_str(), { (float)initialX, (float)initialY + editorFontSize * i }, editorFontSize, 0, WHITE);
				}
				EndShaderMode();
				Vector2 measure = MeasureTextEx(font, editor.shaderFileNames[editor.fileSelectLine].c_str(), editorFontSize, 0);
				DrawRectangle(initialX, initialY + editorFontSize * editor.fileSelectLine, measure.x, measure.y, { 50, 50, 50, 150 });
			} else if (editor.nameSelect) {
				int initialX = width / 2;
				int initialY = height / 2;
				auto f = TextFormat("Save file: %s", editor.nameEditor.c_str());
				DrawTextEx(font, f, { (float)initialX, (float)initialY }, editorFontSize, 0, WHITE);

				EndShaderMode();
				int cursorPos = 0;
				for (int i = 0; i < formatLen + editor.nameEditorCol; i++) {
					char c = f[i];
					GlyphInfo info = GetGlyphInfo(font, (int)c);
					float scaleFactor = (float)editorFontSize / font.baseSize;
					cursorPos += info.advanceX * scaleFactor;
				}
				Color cursorColor = WHITE;
				DrawRectangle(initialX + cursorPos, initialY, editorFontSize / 4, editorFontSize, cursorColor);
			} else {
				EndShaderMode();
				int cursorPos = 0;
				Line l = editor.lines[editor.cursor.line];
				for (int i = 0; i < editor.cursor.col; i++) {
					char c = l.items[i];
					GlyphInfo info = GetGlyphInfo(font, (int)c);
					float scaleFactor = (float)editorFontSize / font.baseSize;
					cursorPos += info.advanceX * scaleFactor;
				}
				Color cursorColor = WHITE;
				DrawRectangle(cursorPos, (editor.cursor.line - editor.topLine) * editorFontSize, editorFontSize / 4, editorFontSize, cursorColor);
			}
		}
		
		//DrawTexture(font.texture, width - font.texture.width, 0, WHITE);

		EndDrawing();
	}

	CloseWindow();

	return 0;
}