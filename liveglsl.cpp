#include <stdio.h>

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
		d = sin(d*8.0 + time);
		d = 0.02 / d;

		outColor = vec4(d, d, d, 1.0);
	}
)";

int main() {
	int width = 1280, height = 720;
	InitWindow(width, height, "liveglsl");

	SetTargetFPS(60);

	int textSize = 20;

	unsigned int fileSize = 0;
	unsigned char *fileData = LoadFileData("..\\..\\..\\Fonts\\AnonymousPro-Regular.ttf", &fileSize);

	Font font = { 0 };
	font.baseSize = textSize;
	font.glyphCount = 95;
	font.glyphs = LoadFontData(fileData, fileSize, textSize, 0, 0, FONT_SDF);
	Image atlas = GenImageFontAtlas(font.glyphs, &font.recs, 95, textSize, 5, 0);
	font.texture = LoadTextureFromImage(atlas);
	UnloadImage(atlas);
	UnloadFileData(fileData);

	Shader textShader = LoadShaderFromMemory(nullptr, textFrag);
	SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

	int cursorCol = 0;
	int cursorRow = 0;

	int charLength = MeasureText("a", textSize);

	Shader liveShader = LoadShaderFromMemory(nullptr, initialCode);
	int resolutionLoc = GetShaderLocation(liveShader, "resolution");
	int timeLoc = GetShaderLocation(liveShader, "time");

	Vector2 resolution = { width, height };

	while (!WindowShouldClose()) {
		if (IsKeyPressed(KEY_LEFT) && cursorRow > 0) cursorRow--;
		if (IsKeyPressed(KEY_RIGHT)) cursorRow++;
		if (IsKeyPressed(KEY_UP) && cursorCol > 0) cursorCol--;
		if (IsKeyPressed(KEY_DOWN)) cursorCol++;

		ClearBackground(DARKGRAY);

		float time = GetTime();
		SetShaderValue(liveShader, resolutionLoc, (const void *)&resolution, SHADER_UNIFORM_VEC2);
		SetShaderValue(liveShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);

		BeginDrawing();

		BeginShaderMode(liveShader);
		DrawRectangle(0, 0, width, height, WHITE);
		EndShaderMode();

		BeginShaderMode(textShader);
		DrawTextEx(font, initialCode, {0, 0}, textSize, 0, WHITE);
		EndShaderMode();
		//DrawRectangle(cursorRow * charLength, cursorCol * textSize, charLength, textSize, GRAY);
		DrawTexture(font.texture, width - font.texture.width, 0, WHITE);

		EndDrawing();
	}

	CloseWindow();

	return 0;
}