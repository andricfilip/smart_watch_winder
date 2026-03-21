#!/bin/sh
set -eu

DEVICE_MODE="${APP_DEVICE_MODE:-single}"
case "$DEVICE_MODE" in
  single|double) ;;
  *) DEVICE_MODE="single" ;;
esac

LOCK_MODE="${APP_LOCK_DEVICE_MODE:-true}"
case "$LOCK_MODE" in
  true|false) ;;
  *) LOCK_MODE="true" ;;
esac

DEFAULT_URL="${APP_DEFAULT_DEVICE_URL:-http://192.168.1.111}"

LOCK_URL="${APP_LOCK_DEVICE_URL:-true}"
case "$LOCK_URL" in
  true|false) ;;
  *) LOCK_URL="true" ;;
esac

APP_LANGUAGE="${APP_LANGUAGE:-sr}"
case "$APP_LANGUAGE" in
  sr|en) ;;
  *) APP_LANGUAGE="sr" ;;
esac

LOCK_LANGUAGE="${APP_LOCK_LANGUAGE:-false}"
case "$LOCK_LANGUAGE" in
  true|false) ;;
  *) LOCK_LANGUAGE="false" ;;
esac

cat > /usr/share/nginx/html/config.js <<EOF
window.APP_CONFIG = {
  defaultDeviceUrl: "${DEFAULT_URL}",
  lockDeviceUrl: ${LOCK_URL},
  deviceMode: "${DEVICE_MODE}",
  lockDeviceMode: ${LOCK_MODE},
  language: "${APP_LANGUAGE}",
  lockLanguage: ${LOCK_LANGUAGE}
};
EOF

exec nginx -g 'daemon off;'
