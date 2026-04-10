#!/system/bin/sh
check_reset_prop() {
  local NAME="$1"
  local EXPECTED="$2"
  local VALUE
  VALUE="$(resetprop "$NAME")"
  [ -z "$VALUE" ] || [ "$VALUE" = "$EXPECTED" ] || resetprop "$NAME" "$EXPECTED"
}


check_reset_prop "ro.product.manufacturer" "Motorola"
check_reset_prop "ro.product.brand" "Motorola"
check_reset_prop "ro.product.marketname" "moto X70 Air Pro"
check_reset_prop "ro.product.model" "XT2601-3"
