BLUE  = \033[1;34m
GREEN = \033[1;32m
RESET = \033[0m

CTRL_CPP_DIR = controllers/fuzzy_cpp
PLANTS_DIR   = plants
SCRIPTS_DIR  = scripts

.PHONY: all plants ctrl_cpp clean setup help

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

clean:
	@echo "$(BLUE)Cleaning everything...$(RESET)"
	$(MAKE) -C $(PLANTS_DIR) clean
	rm -rf $(CTRL_CPP_DIR)/build
	@echo "$(GREEN)Cleanup completed.$(RESET)"

help:
	@echo "Available commands:"
	@echo "  make all       - Compile plants and C++ controller"
	@echo "  make plants    - Compile only the plant simulators in C"
	@echo "  make ctrl_cpp  - Compile only the C++ controller"
	@echo "  make setup_py  - Set up Python environment for fuzzy_py controller"
	@echo "  make clean     - Remove all compiled files"