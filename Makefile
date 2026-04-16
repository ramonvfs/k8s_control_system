BLUE  = \033[1;34m
GREEN = \033[1;32m
RESET = \033[0m

CTRL_CPP_DIR = controllers/fuzzy_cpp
PLANTS_DIR   = plants
SCRIPTS_DIR  = scripts

.PHONY: all plants ctrl_cpp setup_py clean help run_ctrl run_car run_dc_motor run_tank help

all: ctrl_cpp plants
	@echo "$(GREEN)Build complete!$(RESET)"

plants:
	@echo "$(BLUE)Compiling plant simulators...$(RESET)"
	$(MAKE) -C $(PLANTS_DIR)

ctrl_cpp:
	@echo "$(BLUE)Configuring and compiling C++ controller...$(RESET)"
	@mkdir -p $(CTRL_CPP_DIR)/build
	@cd $(CTRL_CPP_DIR)/build && cmake .. && make

setup_py:
	@echo "$(BLUE)Configuring Python environment...$(RESET)"
	python3 -m venv venv
	./venv/bin/pip install -r controllers/fuzzy_py/requirements.txt

run_ctrl:
	@echo "$(BLUE)Initializing C++ controller...$(RESET)"
	@./$(CTRL_CPP_DIR)/build/fuzzy_service

run_car:
	@echo "$(BLUE)Initializing car simulation...$(RESET)"
	@/bin/bash -c "source $(SCRIPTS_DIR)/config.env && ./plants/bin/car"

run_dc_motor:
	@echo "$(BLUE)Initializing DC motor simulation...$(RESET)"
	@/bin/bash -c "source $(SCRIPTS_DIR)/config.env && ./plants/bin/dc_motor"

run_tank:
	@echo "$(BLUE)Initializing tank simulation...$(RESET)"
	@/bin/bash -c "source $(SCRIPTS_DIR)/config.env && ./plants/bin/tank"

clean:
	@echo "$(BLUE)Cleaning everything...$(RESET)"
	$(MAKE) -C $(PLANTS_DIR) clean
	rm -rf $(CTRL_CPP_DIR)/build
	@echo "$(GREEN)Cleanup completed.$(RESET)"

help:
	@echo "$(BLUE)Available commands:$(RESET)"
	@echo "  $(GREEN)make all$(RESET)          - Compile plants and C++ controller"
	@echo "  $(GREEN)make plants$(RESET)       - Compile only the plant simulators in C"
	@echo "  $(GREEN)make ctrl_cpp$(RESET)     - Compile only the C++ controller"
	@echo "  $(GREEN)make setup_py$(RESET)     - Set up Python environment for fuzzy_py"
	@echo "  $(GREEN)make clean$(RESET)        - Remove all compiled files"
	@echo ""
	@echo "$(BLUE)Run Commands:$(RESET)"
	@echo "  $(GREEN)make run_ctrl$(RESET)     - Start the C++ Fuzzy service"
	@echo "  $(GREEN)make run_car$(RESET)      - Start car simulation (loads config.env)"
	@echo "  $(GREEN)make run_dc_motor$(RESET) - Start DC motor simulation (loads config.env)"
	@echo "  $(GREEN)make run_tank$(RESET)     - Start tank simulation (loads config.env)"