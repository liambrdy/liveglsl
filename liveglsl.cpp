#include <cstdio>
#include <vector>
#include <cassert>

#include <raylib.h>

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
};

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
	InitWindow(width, height, "liveglsl");

	SetTargetFPS(60);

	Editor editor = {};
	ImportStringToEditor(&editor, initialCode);

	int fontSize = 50;

	unsigned int fileSize = 0;
	unsigned char *fileData = LoadFileData("..\\..\\..\\Fonts\\AnonymousPro-Regular.ttf", &fileSize);

	Font font = { 0 };
	font.baseSize = fontSize;
	font.glyphCount = 95;
	font.glyphs = LoadFontData(fileData, fileSize, fontSize, 0, 0, FONT_SDF);
	Image atlas = GenImageFontAtlas(font.glyphs, &font.recs, 95, fontSize, 5, 0);
	font.texture = LoadTextureFromImage(atlas);
	UnloadImage(atlas);
	UnloadFileData(fileData);

	Shader textShader = LoadShaderFromMemory(nullptr, textFrag);
	SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

	int charLength = MeasureText("a", fontSize);

	Shader liveShader = LoadShaderFromMemory(nullptr, initialCode);
	int resolutionLoc = GetShaderLocation(liveShader, "resolution");
	int timeLoc = GetShaderLocation(liveShader, "time");

	Vector2 resolution = { width, height };

	while (!WindowShouldClose()) {
		if (IsKeyPressed(KEY_LEFT) && editor.cursorCol > 0) {
			editor.cursorCol--;
		}
		if (IsKeyPressed(KEY_RIGHT) && editor.cursorCol < editor.lines[editor.cursorLine].items.size()) {
			editor.cursorCol++;
		}
		if (IsKeyPressed(KEY_UP) && editor.cursorLine > 0) {
			editor.cursorLine--;
			editor.cursorCol = 0;
		}
		if (IsKeyPressed(KEY_DOWN)) {
			editor.cursorLine++;
			editor.cursorCol = 0;
		}
		if (IsKeyPressed(KEY_ENTER)) {
			if (IsKeyDown(KEY_LEFT_ALT)) {
				char *shaderCode = EditorToString(&editor);
				Shader tmpShader = LoadShaderFromMemory(nullptr, shaderCode);
				if (IsShaderReady(tmpShader)) {
					liveShader = tmpShader;
					resolutionLoc = GetShaderLocation(liveShader, "resolution");
					timeLoc = GetShaderLocation(liveShader, "time");
				}
				free(shaderCode);
			} else {
				Line l = {};
				editor.lines.insert(editor.lines.begin() + editor.cursorLine, l);
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
		}
		if (IsKeyPressed(KEY_TAB)) {
			auto &l = editor.lines[editor.cursorLine];
			l.items.insert(l.items.begin() + editor.cursorCol, 5, ' ');
			editor.cursorCol += 5;
		}
		char chr = (char)GetCharPressed();
		if (chr != 0) {
			auto &line = editor.lines[editor.cursorLine];
			line.items.insert(line.items.begin() + editor.cursorCol, chr);
			editor.cursorCol++;
		}

		ClearBackground(DARKGRAY);

		float time = GetTime();
		SetShaderValue(liveShader, resolutionLoc, (const void *)&resolution, SHADER_UNIFORM_VEC2);
		SetShaderValue(liveShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);

		BeginDrawing();

		BeginShaderMode(liveShader);
		DrawRectangle(0, 0, width, height, WHITE);
		EndShaderMode();

		BeginShaderMode(textShader);
		// DrawTextEx(font, initialCode, {0, 0}, textSize, 0, WHITE);

		for (int i = 0; i < editor.lines.size(); i++) {
			Line l = editor.lines[i];
			int x = 0;
			if (!l.items.empty()) {
				for (int j = 0; j < l.items.size(); j++) {
					char c = l.items[j];
					GlyphInfo info = GetGlyphInfo(font, (int)c);
					DrawTextCodepoint(font, (int)c, { (float)x, (float)i * fontSize }, fontSize, WHITE);
					x += info.advanceX;
				}
			}
			//DrawTextCodepoints(font, (const int *)l.items.data(), l.items.size(), { 0, (float)i * textSize }, textSize, 0, WHITE);
		}

		EndShaderMode();
		int cursorPos = 0;
		Line l = editor.lines[editor.cursorLine];
		for (int i = 0; i < editor.cursorCol; i++) {
			char c = l.items[i];
			GlyphInfo info = GetGlyphInfo(font, (int)c);
			cursorPos += info.advanceX;
		}
		Color cursorColor = WHITE;
		DrawRectangle(cursorPos, editor.cursorLine * fontSize, fontSize / 4, fontSize, cursorColor);
		
		//DrawTexture(font.texture, width - font.texture.width, 0, WHITE);

		EndDrawing();
	}

	CloseWindow();

	return 0;
}