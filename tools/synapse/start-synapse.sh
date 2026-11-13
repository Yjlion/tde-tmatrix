#!/bin/bash
# Starts a throw-away Synapse homeserver for the tests and screenshots on
# http://127.0.0.1:8008 (server name example.org) and creates the users
# alice, bob and carol (password "secret-password").
#
#   tools/synapse/start-synapse.sh [state directory]
#
# Synapse comes from pip: python3 -m venv /opt/synapse &&
# /opt/synapse/bin/pip install matrix-synapse (or set SYNAPSE_PY).
set -e
DIR=${1:-/tmp/synapse}
PY=${SYNAPSE_PY:-/opt/synapse/bin/python}
mkdir -p "$DIR"
cd "$DIR"
if [ ! -f homeserver.yaml ]; then
	"$PY" -m synapse.app.homeserver --server-name example.org --config-path homeserver.yaml \
		--generate-config --report-stats=no >/dev/null
	sed -i '/^registration_shared_secret/d' homeserver.yaml
	echo >> homeserver.yaml
	cat >> homeserver.yaml <<'YAML'
registration_shared_secret: "tmatrix-test-secret"
enable_registration: false
suppress_key_server_warning: true
trusted_key_servers: []
room_list_publication_rules:
  - action: allow
user_directory:
  enabled: true
  search_all_users: true
rc_message: {per_second: 1000, burst_count: 1000}
rc_registration: {per_second: 1000, burst_count: 1000}
rc_login:
  address: {per_second: 1000, burst_count: 1000}
  account: {per_second: 1000, burst_count: 1000}
  failed_attempts: {per_second: 1000, burst_count: 1000}
rc_joins:
  local: {per_second: 1000, burst_count: 1000}
  remote: {per_second: 1000, burst_count: 1000}
rc_invites:
  per_room: {per_second: 1000, burst_count: 1000}
  per_user: {per_second: 1000, burst_count: 1000}
rc_key_requests: {per_second: 1000, burst_count: 1000}
rc_3pid_validation: {per_second: 1000, burst_count: 1000}
rc_admin_redaction: {per_second: 1000, burst_count: 1000}
rc_media_create: {per_second: 1000, burst_count: 1000}
rc_delayed_event_mgmt: {per_second: 1000, burst_count: 1000}
YAML
	# listen on IPv4 only
	sed -i "s/bind_addresses: \['::1', '127.0.0.1'\]/bind_addresses: ['127.0.0.1']/" homeserver.yaml
fi
"$PY" -m synapse.app.homeserver --config-path homeserver.yaml --daemonize
for i in $(seq 60); do
	curl -sf http://127.0.0.1:8008/_matrix/client/versions >/dev/null && break
	sleep 0.5
done
curl -sf http://127.0.0.1:8008/_matrix/client/versions >/dev/null || { echo "Synapse did not start"; tail -30 homeserver.log; exit 1; }
for u in alice bob carol; do
	"$PY" -m synapse._scripts.register_new_matrix_user -c homeserver.yaml -u $u -p secret-password --no-admin \
		http://127.0.0.1:8008 >/dev/null 2>&1 || true
done
echo "Synapse runs on http://127.0.0.1:8008 (users alice, bob, carol)"
