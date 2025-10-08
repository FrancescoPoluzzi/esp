.PHONY: hw sw mirror-filelist mcu-gen

ACCEL_NAME := xheep
VLOG_INCDIRS  += /home/poluzzi/esp/accelerators/third-party/xheep/vlog_incdir
VLOG_FILELIST += /home/poluzzi/esp/accelerators/third-party/xheep/xheep.sverilog
export VLOG_INCDIRS VLOG_FILELIST
$(info [xheep] VLOG_FILELIST=$(VLOG_FILELIST))
$(info [xheep] VLOG_INCDIRS=$(VLOG_INCDIRS))

# roots
XHEEP_ROOT := $(PWD)
OUT_DIR    := $(XHEEP_ROOT)/out
OBI_ROOT   := $(XHEEP_ROOT)/ip/obi
XH_ROOT    := $(XHEEP_ROOT)/ip/x-heep

# 1) run the X-HEEP generator, then 2) mirror filelist into out/
hw: mcu-gen mirror-filelist

mcu-gen:
	$(MAKE) -C $(XH_ROOT) mcu-gen

# Copy every non-empty, non-comment line from xheep.sverilog into out/<same path>.
# Lines starting with "obi/" are sourced from ip/obi/..., and "x-heep/" from ip/x-heep/...
mirror-filelist:
	@OUT="$(OUT_DIR)"; OBI="$(OBI_ROOT)"; XH="$(XH_ROOT)"; FL="$(VLOG_FILELIST)"; \
	echo "[xheep] Mirroring filelist -> $$OUT"; \
	while IFS= read -r rel; do \
	  [ -z "$$rel" ] && continue; \
	  case "$$rel" in \#*) continue ;; esac; \
	  prefix=$${rel%%/*}; rest=$${rel#*/}; \
	  if [ "$$prefix" = "obi" ]; then src="$$OBI/$$rest"; \
	  elif [ "$$prefix" = "x-heep" ]; then src="$$XH/$$rest"; \
	  else echo "[xheep][ERROR] Unknown prefix in $$rel"; exit 1; fi; \
	  [ -f "$$src" ] || { echo "[xheep][ERROR] Source not found: $$src (from $$rel)"; exit 1; }; \
	  dst="$$OUT/$$rel"; \
	  mkdir -p "$$(dirname "$$dst")"; \
	  if ! cmp -s "$$src" "$$dst" 2>/dev/null; then cp "$$src" "$$dst"; echo "[xheep] COPIED  $$src -> $$dst"; fi; \
	done < "$$FL"


sw:
	# clean re-config so a previous bad cache can't persist
	rm -rf $(XH_ROOT)/sw/build/CMakeCache.txt $(XH_ROOT)/sw/build/CMakeFiles
	# run x-heep app with leaked flags scrubbed but keep required toolchain/env
	env -u CFLAGS -u CXXFLAGS -u CPPFLAGS -u ASFLAGS -u LDFLAGS \
	    -u CMAKE_C_FLAGS -u CMAKE_CXX_FLAGS -u CMAKE_ASM_FLAGS -u CMAKE_EXE_LINKER_FLAGS \
	    -u ARCH -u MARCH -u XCFLAGS -u XCXXFLAGS -u MAKEFLAGS \
	    RISCV="/home/poluzzi/riscv32imc" \
	    CC="riscv32-unknown-elf-gcc" CXX="riscv32-unknown-elf-g++" \
	    PY="python3" \
	    PATH="/home/poluzzi/riscv32imc/bin:$(PATH)" \
	    $(MAKE) -C $(XH_ROOT) app PROJECT=esp_app
	mkdir -p $(XH_ROOT)/sw/generated
	python3 $(XH_ROOT)/util/c_gen.py \
		$(XH_ROOT)/sw/generated/xheep_firmware.h \
		$(XH_ROOT)/sw/build/main.bin
	mkdir -p $(ESP_ROOT)/soft/common/apps/baremetal/xheep
	mkdir -p $(ESP_ROOT)/soft/common/apps/baremetal/xheep/common
	cp $(XH_ROOT)/sw/generated/xheep_firmware.h $(ESP_ROOT)/soft/common/apps/baremetal/xheep
	cp $(XH_ROOT)/sw/device/lib/runtime/core_v_mini_mcu.h $(ESP_ROOT)/soft/common/apps/baremetal/xheep
	cp $(XH_ROOT)/sw/device/lib/drivers/power_manager/power_manager_regs.h $(ESP_ROOT)/soft/common/apps/baremetal/xheep
	cp -r $(XH_ROOT)/sw/applications/esp_app/common/. $(ESP_ROOT)/soft/common/apps/baremetal/xheep/common

vsim:
	$(info CIAOCIAO)
