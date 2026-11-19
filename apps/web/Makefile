.PHONY: dev test test-backend test-frontend contract build compose-test clean-test

dev:
	docker compose up --build

test: test-backend test-frontend

test-backend:
	cd backend && pytest

test-frontend:
	cd frontend && npm run test && npm run build

contract:
	./scripts/check_openapi.sh

build:
	docker compose build

compose-test:
	docker compose --project-name aplicacion-web-test -f compose.yaml -f compose.test.yaml up --build --abort-on-container-exit --exit-code-from e2e

clean-test:
	docker compose --project-name aplicacion-web-test -f compose.yaml -f compose.test.yaml down --volumes --remove-orphans
