.PHONY: hw sw mirror-filelist update-xheep-sverilog update-xheep-verilog mcu-gen questa

ACCEL_NAME := xheep
VLOG_INCDIRS  += $(ESP_TOP)/accelerators/third-party/xheep/vlog_incdir
VLOG_FILELIST += $(ESP_TOP)/accelerators/third-party/xheep/xheep.sverilog
export VLOG_INCDIRS VLOG_FILELIST
$(info [xheep] VLOG_FILELIST=$(VLOG_FILELIST))
$(info [xheep] VLOG_INCDIRS=$(VLOG_INCDIRS))

# roots
XHEEP_ROOT := $(PWD)
OUT_DIR    := $(XHEEP_ROOT)/out
OBI_ROOT   := $(XHEEP_ROOT)/ip/obi
XH_ROOT    := $(XHEEP_ROOT)/ip/x-heep

MODELSIM_FILELIST := build/openhwgroup.org_systems_core-v-mini-mcu_0.3.0/sim-modelsim

# 1) run the X-HEEP generator, then 2) mirror filelist into out/
hw: mcu-gen questa update-xheep-sverilog update-xheep-verilog mirror-filelist

mcu-gen:
	$(MAKE) -C $(XH_ROOT) mcu-gen X_HEEP_CFG=configs/esp_heep.hjson

questa:
	$(MAKE) -C $(XH_ROOT) questasim-sim

update-xheep-sverilog:
	@set -e; \
	TCL="$(XH_ROOT)/$(MODELSIM_FILELIST)/edalize_build_rtl.tcl"; \
	SV="$(XHEEP_ROOT)/xheep.sverilog"; \
	[ -f "$$TCL" ] || { echo "[xheep][ERROR] Missing $$TCL"; exit 1; }; \
	t1="$$(mktemp)"; t2="$$(mktemp)"; \
	awk '/-work[ \t]+work/ { for (i=NF; i>0; i--) if ($$i ~ /\.sv$$/) { print $$i; break } }' "$$TCL" > "$$t1"; \
	awk -v genpref="x-heep/$(MODELSIM_FILELIST)/" -v incdir="$(XHEEP_ROOT)/vlog_incdir" '{ p=$$0; if (p ~ /^generated\//) { p = genpref p } else if (p ~ /^(\.\.\/){3}/) { sub(/^(\.\.\/){3}/, "", p); p = "x-heep/" p } else { p = "x-heep/" p } bn = p; sub(/^.*\//, "", bn); if (system("[ -f \"" incdir "/" bn "\" ]") == 0) { next } print p }' "$$t1" > "$$t2"; \
	{ grep -v '^x-heep' "$$SV" 2>/dev/null || true; } > "$$SV.new"; \
	cat "$$t2" >> "$$SV.new"; \
	mv "$$SV.new" "$$SV"; \
	rm -f "$$t1" "$$t2"; \
	echo "[xheep] Updated $$SV from $$TCL"

update-xheep-verilog:
	@set -e; \
	TCL="$(XH_ROOT)/$(MODELSIM_FILELIST)/edalize_build_rtl.tcl"; \
	VL="$(XHEEP_ROOT)/xheep.verilog"; \
	[ -f "$$TCL" ] || { echo "[xheep][ERROR] Missing $$TCL"; exit 1; }; \
	t1="$$(mktemp)"; t2="$$(mktemp)"; \
	awk '/-work[ \t]+work/ { for (i=NF; i>0; i--) if ($$i ~ /\.v$$/) { print $$i; break } }' "$$TCL" > "$$t1"; \
	awk -v genpref="x-heep/$(MODELSIM_FILELIST)/" -v incdir="$(XHEEP_ROOT)/vlog_incdir" '{ p=$$0; if (p ~ /^generated\//) { p = genpref p } else if (p ~ /^(\.\.\/){3}/) { sub(/^(\.\.\/){3}/, "", p); p = "x-heep/" p } else { p = "x-heep/" p } bn = p; sub(/^.*\//, "", bn); if (system("[ -f \"" incdir "/" bn "\" ]") == 0) { next } print p }' "$$t1" > "$$t2"; \
	{ grep -v '^x-heep' "$$VL" 2>/dev/null || true; } > "$$VL.new"; \
	cat "$$t2" >> "$$VL.new"; \
	mv "$$VL.new" "$$VL"; \
	rm -f "$$t1" "$$t2"; \
	echo "[xheep] Updated $$VL from $$TCL"


# Copy every non-empty, non-comment line from xheep.sverilog AND xheep.verilog into out/<same path>.
# Lines starting with "obi/" are sourced from ip/obi/..., and "x-heep/" from ip/x-heep/...
mirror-filelist:
	@OUT="$(OUT_DIR)"; OBI="$(OBI_ROOT)"; XH="$(XH_ROOT)"; FL="$(VLOG_FILELIST)"; VL="$(XHEEP_ROOT)/xheep.verilog"; \
	echo "[xheep] Mirroring filelist -> $$OUT"; \
	for LIST in "$$FL" "$$VL"; do \
	  [ -f "$$LIST" ] || { echo "[xheep][WARN] Missing list $$LIST, skipping"; continue; }; \
	  while IFS= read -r rel; do \
	    [ -z "$$rel" ] && continue; \
	    case "$$rel" in \#*) continue ;; esac; \
	    prefix=$${rel%%/*}; rest=$${rel#*/}; \
	    if [ "$$prefix" = "obi" ]; then src="$$OBI/$$rest"; \
	    elif [ "$$prefix" = "x-heep" ]; then src="$$XH/$$rest"; \
	    else echo "[xheep][ERROR] Unknown prefix in $$rel (from $$LIST)"; exit 1; fi; \
	    [ -f "$$src" ] || { echo "[xheep][ERROR] Source not found: $$src (from $$rel in $$LIST)"; exit 1; }; \
	    dst="$$OUT/$$rel"; \
	    mkdir -p "$$(dirname "$$dst")"; \
	    if ! cmp -s "$$src" "$$dst" 2>/dev/null; then cp "$$src" "$$dst"; echo "[xheep] COPIED  $$src -> $$dst"; fi; \
	  done < "$$LIST"; \
	done

sw:
	rm -rf $(XH_ROOT)/sw/build
	$(MAKE) -C $(XH_ROOT) RISCV=/home/francesco/tools/riscv  ARCH=rv32imc app PROJECT=esp_app LINKER=on_chip TARGET=sim
	mkdir -p $(XH_ROOT)/sw/generated
	python3 $(ESP_TOP)/accelerators/third-party/xheep/ip/x-heep/util/hex_to_c.py \
		$(XH_ROOT)/sw/generated/xheep_firmware.h \
		$(XH_ROOT)/sw/build/main.hex
	mkdir -p $(ESP_TOP)/soft/common/apps/baremetal/xheep
	mkdir -p $(ESP_TOP)/soft/common/apps/baremetal/xheep/common
	cp $(XH_ROOT)/sw/generated/xheep_firmware.h $(ESP_TOP)/soft/common/apps/baremetal/xheep
	cp $(XH_ROOT)/sw/device/lib/runtime/core_v_mini_mcu.h $(ESP_TOP)/soft/common/apps/baremetal/xheep
	cp $(XH_ROOT)/sw/device/lib/drivers/power_manager/power_manager_regs.h $(ESP_TOP)/soft/common/apps/baremetal/xheep
	cp -r $(XH_ROOT)/sw/applications/esp_app/common/. $(ESP_TOP)/soft/common/apps/baremetal/xheep/common

	cp $(XH_ROOT)/sw/generated/xheep_firmware.h $(ESP_TOP)/socs/xilinx-vc707-xc7vx485t
	cp $(XH_ROOT)/sw/device/lib/runtime/core_v_mini_mcu.h $(ESP_TOP)/socs/xilinx-vc707-xc7vx485t
	cp $(XH_ROOT)/sw/device/lib/drivers/power_manager/power_manager_regs.h $(ESP_TOP)/socs/xilinx-vc707-xc7vx485t
	cp $(XH_ROOT)/sw/device/lib/drivers/soc_ctrl/soc_ctrl_regs.h $(ESP_TOP)/socs/xilinx-vc707-xc7vx485t
	cp -r $(XH_ROOT)/sw/applications/esp_app/common/.  $(ESP_TOP)/socs/xilinx-vc707-xc7vx485t

vsim:
	$(info CIAOCIAO)
