#!/bin/bash
set -e

echo ""
echo "=============================="
echo "🚀  DECS PROJECT - PHASE 1 DEMO"
echo "=============================="
echo ""

SCRIPT_DIR="$(dirname "$0")"
PROJECT_ROOT="$SCRIPT_DIR/.."

echo "▶ Starting PostgreSQL (macOS)..."
brew services start postgresql

sleep 2

echo "▶ Initializing DB..."
createdb decs_project 2>/dev/null || echo "DB already exists."
psql decs_project < "$PROJECT_ROOT/sql/init_db.sql"
echo "Table 'kvstore' ensured."
echo ""

echo "▶ Building server..."
chmod +x "$PROJECT_ROOT/scripts/build_mac.sh"
"$PROJECT_ROOT/scripts/build_mac.sh"
echo ""

echo "▶ Starting server..."
"$PROJECT_ROOT/server/server" "host=127.0.0.1 port=5432 dbname=decs_project user=$(whoami)" &
SERVER_PID=$!

sleep 1
echo "✅ Server running at http://127.0.0.1:8080"
echo ""
echo "Try these:"
echo 'curl "http://127.0.0.1:8080/create?key=1&value=hello"'
echo 'curl "http://127.0.0.1:8080/read?key=1"'
echo 'curl "http://127.0.0.1:8080/delete?key=1"'
echo ""

read -p "Press ENTER to stop server..."

kill $SERVER_PID
echo "🛑 Server stopped."
