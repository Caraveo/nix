.PHONY: build run headless test export
build:
	./scripts/build.sh
run:
	./scripts/run.sh
headless:
	./scripts/run.sh --headless
test:
	python3 tests/smoke.py
export:
	python3 scripts/memory.py export build/memories.img --output exports
