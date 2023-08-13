#include <cstdio>
#include <cassert>
#include <memory>
#include <string>

const char *templateBegin = R"(#ifndef FONT_H
#define FONT_H

int fontDataSize = %d;
unsigned char fontData[%d] = {
)";

const char *templateEnd = R"(
};

#endif // FONT_H
)";

int main(int argc, char **argv) {
	assert(argc > 2);

	char *path = argv[1];
	char *output = argv[2];

	FILE *f = fopen(path, "rb");
	if (!f) {
		printf("ERROR: Failed to open file: %s\n", path);
		return 1;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	rewind(f);

	unsigned char *buffer = (unsigned char *)malloc(size);
	assert(buffer);
	if (buffer) {
		fread(buffer, size, 1, f);
	}

	fclose(f);

	f = fopen(output, "w");
	if (!f) {
		printf("ERROR: Failed to open file: %s\n", output);
		return 1;
	}

	char comma = ',';

	auto sizeStr = std::to_string(size);

	char buf[1024] = {};
	snprintf(buf, 1024, templateBegin, size, size);

	fwrite(buf, strlen(buf), 1, f);
	fwrite(sizeStr.c_str(), sizeStr.size(), 1, f);
	for (int i = 0; i < size; i++) {
		int v = (int)buffer[i];
		auto s = std::to_string(v);
		fwrite(s.c_str(), s.size(), 1, f);
		fwrite(&comma, 1, 1, f);
	}
	fwrite(templateEnd, strlen(templateEnd), 1, f);

	fclose(f);

	return 0;
}