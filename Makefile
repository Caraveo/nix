.PHONY: build run headless test export
build:
	./scripts/build.sh
run:
	./scripts/run.sh
headless:
	./scripts/run.sh --headless
test:
	python3 tests/smoke.py
	python3 tests/power.py
export:
	python3 scripts/memory.py export build/memories.img --output exports

.PHONY: model ai-test package
model:
	./scripts/setup-model.sh
ai-test:
	python3 tests/ai_guest.py
package: build
	python3 scripts/package.py
