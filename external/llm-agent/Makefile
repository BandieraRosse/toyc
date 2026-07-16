#
# Makefile — llm-agent（用 gcc + libcurl + cJSON 编译）
#
# 系统依赖：
#   sudo apt install libcurl4-openssl-dev   # Debian/Ubuntu
#   sudo dnf install libcurl-devel          # Fedora
#
# 若 libcurl 不可用，编译会打印安装说明并生成 stub。
#
# 用法：
#   make                       编译 llm-agent
#   make BUILD_DIR=build/llm   指定输出目录
#   make clean                 删除产物
#   make run                   编译并执行（默认 prompt）
#

CC       := gcc
CFLAGS   := -Wall -Wextra -g -I third_party/cJSON
BUILD_DIR ?= ../../build/llm-agent

# 检测 libcurl 是否可用
HAS_CURL := $(shell echo '#include <curl/curl.h>' | $(CC) -fsyntax-only -x c - 2>/dev/null && echo yes)

ifeq ($(HAS_CURL),yes)
CURL_CFLAGS  :=
CURL_LDFLAGS := -lcurl
CURL_OBJS    := $(BUILD_DIR)/llm_client.o
CURL_MSG     := 有 libcurl
else
CURL_CFLAGS  := -DNO_CURL
CURL_LDFLAGS :=
CURL_OBJS    := $(BUILD_DIR)/llm_client_stub.o
CURL_MSG     := 无 libcurl（使用 stub）
endif

OBJS   := $(addprefix $(BUILD_DIR)/, main.o json_util.o buffer.o cjson.o) $(CURL_OBJS)
TARGET := $(BUILD_DIR)/llm-agent

.PHONY: all clean run

all: $(TARGET)
	@printf "  llm-agent: $(CURL_MSG)\n"

$(TARGET): $(OBJS) | $(BUILD_DIR)
	@printf "  LD   %s\n" "$@"
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@size=$$(stat --printf="%s" $@ 2>/dev/null || stat -f%z $@ 2>/dev/null); \
		printf "  => %s（%d 字节）\n" "$@" "$${size:-?}"

# cJSON 单独编译（源码在子目录下）
$(BUILD_DIR)/cjson.o: third_party/cJSON/cJSON.c | $(BUILD_DIR)
	@printf "  CC   %s\n" "$<"
	$(CC) $(CFLAGS) $(CURL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@printf "  CC   %s\n" "$<"
	$(CC) $(CFLAGS) $(CURL_CFLAGS) -c $< -o $@

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

run: all
	./$(TARGET) --prompt "Hello, who are you?"
