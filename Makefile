# =============================================================================
# RISC-V OS 多实验内核构建系统（优化版）
# =============================================================================

# 使用 bash，便于在规则中写 if/for 等
SHELL := /bin/bash

# -----------------------------------------------------------------------------
# 工具链配置
# -----------------------------------------------------------------------------
CROSS_COMPILE ?= riscv64-unknown-elf-

CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)ld
AS      := $(CROSS_COMPILE)gcc
OBJDUMP := $(CROSS_COMPILE)objdump

# -----------------------------------------------------------------------------
# QEMU 配置
# -----------------------------------------------------------------------------
QEMU        ?= qemu-system-riscv64
QEMU_FLAGS  ?= -machine virt -nographic -bios none
QEMU_KERNEL := -kernel

# QEMU 运行超时时间 (秒)；仅 run-all 时使用
QEMU_TIMEOUT ?= 5

# -----------------------------------------------------------------------------
# 自动发现实验目录：exp1 / exp2 / ...
# -----------------------------------------------------------------------------
EXPERIMENTS := $(shell find . -maxdepth 1 -type d -name 'exp*' | sed 's|^\./||' | sort)

# 默认执行 help，直接 make 不会什么都不干
.DEFAULT_GOAL := help

# 伪目标声明
.PHONY: all help list build run dis clean $(EXPERIMENTS)

# -----------------------------------------------------------------------------
# 通用编译 / 链接选项
# -----------------------------------------------------------------------------
CFLAGS := \
	-Wall \
	-O2 \
	-ffreestanding \
	-fno-builtin \
	-nostdlib \
	-march=rv64im_zicsr_zifencei \
	-mabi=lp64 \
	-mcmodel=medany

ASFLAGS      := $(CFLAGS)
LDFLAGS_BASE := -nostdlib

# all 保留，防止习惯性 make all 报错
all: help

# -----------------------------------------------------------------------------
# 实验列表
# -----------------------------------------------------------------------------
list:
	@echo "可用实验："
	@for exp in $(EXPERIMENTS); do \
		echo "  $$exp"; \
	done

# 把所有实验目标声明为伪目标给个空规则，避免 “Nothing to be done for 'expX'”
$(EXPERIMENTS):
	@:

# -----------------------------------------------------------------------------
# 为每个实验生成：路径 / 源文件 / 目标 / 规则
# -----------------------------------------------------------------------------
define EXP_RULES
# ---------- 路径与目标 ----------
EXP_$(1)_BUILD_DIR := build/$(1)
EXP_$(1)_ELF       := $$(EXP_$(1)_BUILD_DIR)/kernel.elf
EXP_$(1)_DUMP      := $$(EXP_$(1)_BUILD_DIR)/kernel.dump

# ---------- 源文件自动发现 ----------
EXP_$(1)_SRCS_C := $$(shell find "$(1)" -name '*.c' | sort)
EXP_$(1)_SRCS_S := $$(shell find "$(1)" -name '*.S' | sort)

EXP_$(1)_OBJS_C := $$(patsubst $(1)/%.c,$$(EXP_$(1)_BUILD_DIR)/%.o,$$(EXP_$(1)_SRCS_C))
EXP_$(1)_OBJS_S := $$(patsubst $(1)/%.S,$$(EXP_$(1)_BUILD_DIR)/%.o,$$(EXP_$(1)_SRCS_S))
EXP_$(1)_OBJS   := $$(EXP_$(1)_OBJS_C) $$(EXP_$(1)_OBJS_S)

# ---------- 链接脚本与头文件路径 ----------
EXP_$(1)_LINKER   := $$(shell find "$(1)" -name '*.ld' | head -1)
EXP_$(1)_INCLUDES := $$(shell find "$(1)" -type d -name 'include' | head -1)

# 若存在 include 目录，则优先使用；否则退回到实验根目录
ifneq ($$(EXP_$(1)_INCLUDES),)
EXP_$(1)_CFLAGS := $$(CFLAGS) -I$$(EXP_$(1)_INCLUDES)
else
EXP_$(1)_CFLAGS := $$(CFLAGS) -I$(1)
endif

# 若存在链接脚本，则使用之；否则仅使用基础 LDFLAGS
ifneq ($$(EXP_$(1)_LINKER),)
EXP_$(1)_LDFLAGS := $$(LDFLAGS_BASE) -T $$(EXP_$(1)_LINKER)
else
EXP_$(1)_LDFLAGS := $$(LDFLAGS_BASE)
endif

# ---------- 链接规则 ----------
$$(EXP_$(1)_ELF): $$(EXP_$(1)_OBJS)
	@echo "[LD]  $(1) -> $$@"
	@mkdir -p $$(dir $$@)
	$$(LD) $$(EXP_$(1)_LDFLAGS) -o $$@ $$^

# ---------- C 源文件编译 ----------
$$(EXP_$(1)_BUILD_DIR)/%.o: $(1)/%.c
	@echo "[CC]  $(1): $$<"
	@mkdir -p $$(dir $$@)
	$$(CC) $$(EXP_$(1)_CFLAGS) -c $$< -o $$@

# ---------- 汇编源文件编译 ----------
$$(EXP_$(1)_BUILD_DIR)/%.o: $(1)/%.S
	@echo "[AS]  $(1): $$<"
	@mkdir -p $$(dir $$@)
	$$(AS) $$(ASFLAGS) -c $$< -o $$@
endef

# 展开所有实验规则
$(foreach exp,$(EXPERIMENTS),$(eval $(call EXP_RULES,$(exp))))

# -----------------------------------------------------------------------------
# 命令解析：make build exp1 / make run exp1 / make dis exp1 / make clean [exp1]
# -----------------------------------------------------------------------------
# EXP 默认取第二个命令参数，如：make build exp1
EXP ?= $(word 2,$(MAKECMDGOALS))

CUR_ELF   = $(EXP_$(EXP)_ELF)
CUR_DUMP  = $(EXP_$(EXP)_DUMP)
CUR_BUILD = $(EXP_$(EXP)_BUILD_DIR)

# 检查 EXP 是否合法的小工具
define CHECK_EXP
@if [ -z "$(EXP)" ]; then \
	echo "用法: make $(1) <exp>  例如: make $(1) exp1"; \
	exit 1; \
fi; \
if ! echo "$(EXPERIMENTS)" | grep -qw "$(EXP)"; then \
	echo "错误: 未知实验 '$(EXP)'"; \
	exit 1; \
fi
endef

# -----------------------------------------------------------------------------
# build：构建指定实验
# -----------------------------------------------------------------------------
build: $(CUR_ELF)
	$(call CHECK_EXP,build)
	@echo "构建完成: $(CUR_ELF)"

# -----------------------------------------------------------------------------
# run：无参数 -> 顺序运行所有实验；带参数 -> 运行指定实验
# -----------------------------------------------------------------------------
run:
	@if [ -z "$(EXP)" ]; then \
		echo "================ 依次运行所有实验 ================"; \
		for exp in $(EXPERIMENTS); do \
			echo ""; \
			echo ">>> 运行 $$exp ($(QEMU_TIMEOUT) 秒后自动退出)..."; \
			echo "------------------------------------------------"; \
			$(MAKE) -s build $$exp; \
			timeout --foreground $(QEMU_TIMEOUT) $(QEMU) $(QEMU_FLAGS) $(QEMU_KERNEL) build/$$exp/kernel.elf || true; \
			echo ""; \
		done; \
		echo "================ 所有实验运行完成 ================"; \
	else \
		if ! echo "$(EXPERIMENTS)" | grep -qw "$(EXP)"; then \
			echo "错误: 未知实验 '$(EXP)'"; \
			exit 1; \
		fi; \
		echo "启动 QEMU 运行 $(EXP)..."; \
		$(MAKE) -s build $(EXP); \
		$(QEMU) $(QEMU_FLAGS) $(QEMU_KERNEL) $(CUR_ELF); \
	fi

# -----------------------------------------------------------------------------
# dis：生成反汇编
# -----------------------------------------------------------------------------
dis: $(CUR_ELF)
	$(call CHECK_EXP,dis)
	@echo "生成反汇编: $(EXP)..."
	$(OBJDUMP) -d $(CUR_ELF) > $(CUR_DUMP)
	@echo "反汇编文件: $(CUR_DUMP)"

# -----------------------------------------------------------------------------
# clean：无参数 -> 清理所有实验；带参数 -> 只清理指定实验
# -----------------------------------------------------------------------------
clean:
	@if [ -z "$(EXP)" ]; then \
		echo "清理所有构建文件..."; \
		rm -rf build; \
	else \
		if ! echo "$(EXPERIMENTS)" | grep -qw "$(EXP)"; then \
			echo "错误: 未知实验 '$(EXP)'"; \
			exit 1; \
		fi; \
		echo "清理构建文件: $(EXP)..."; \
		rm -rf $(CUR_BUILD); \
	fi

# -----------------------------------------------------------------------------
# 帮助信息
# -----------------------------------------------------------------------------
help:
	@echo "================ RISC-V 多实验构建系统 ================"
	@echo "基础用法："
	@echo "  make list            # 查看所有实验"
	@echo "  make build <exp>     # 构建指定实验"
	@echo "  make run             # 依次运行所有实验(默认超时5秒)"
	@echo "  make run   <exp>     # 运行指定实验，直到手动退出"
	@echo "  make dis   <exp>     # 生成指定实验的反汇编"
	@echo ""
	@echo "清理："
	@echo "  make clean           # 清理所有实验的构建文件"
	@echo "  make clean <exp>     # 仅清理指定实验"
	@echo ""
	@echo "高级用法："
	@echo "  CROSS_COMPILE=<前缀> make build exp1"
	@echo "  QEMU_TIMEOUT=10 make run"
	@echo "  QEMU='qemu-system-riscv64 -s -S' make run exp1"
	@echo "======================================================="
