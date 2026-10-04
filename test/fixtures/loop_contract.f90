subroutine loop32_positive_unit(last, cap, values, trips, final_value)
  use iso_fortran_env, only: int32
  implicit none
  integer(int32), intent(in) :: last, cap
  integer(int32), intent(out) :: values(4), trips, final_value
  integer(int32) :: iterator
  trips = 0
  do iterator = 5, last
    trips = trips + 1
    values(trips) = iterator
    if (trips >= cap) exit
  end do
  final_value = iterator
end subroutine

subroutine loop32_positive_edge(last, trips, final_value)
  use iso_fortran_env, only: int32
  implicit none
  integer(int32), intent(in) :: last
  integer(int32), intent(out) :: trips, final_value
  integer(int32) :: iterator
  trips = 0
  do iterator = 2147483647, last
    trips = trips + 1
  end do
  final_value = iterator
end subroutine

subroutine loop8(first, last, stride, cap, values, trips, final_value)
  use iso_fortran_env, only: int8
  implicit none
  integer(int8), intent(in) :: first, last, stride
  integer, intent(in) :: cap
  integer(int8), intent(out) :: values(4), final_value
  integer, intent(out) :: trips
  integer(int8) :: iterator
  trips = 0
  do iterator = first, last, stride
    trips = trips + 1
    values(trips) = iterator
    if (trips >= cap) exit
  end do
  final_value = iterator
end subroutine

subroutine loop_real4(first, last, stride, cap, values, trips, final_value)
  use iso_fortran_env, only: real32
  implicit none
  real(real32), intent(in) :: first, last, stride
  integer, intent(in) :: cap
  real(real32), intent(out) :: values(16), final_value
  integer, intent(out) :: trips
  real(real32) :: iterator
  trips = 0
  do iterator = first, last, stride
    trips = trips + 1
    values(trips) = iterator
    if (trips >= cap) exit
  end do
  final_value = iterator
end subroutine

subroutine loop_real8(first, last, stride, cap, values, trips, final_value)
  use iso_fortran_env, only: real64
  implicit none
  real(real64), intent(in) :: first, last, stride
  integer, intent(in) :: cap
  real(real64), intent(out) :: values(16), final_value
  integer, intent(out) :: trips
  real(real64) :: iterator
  trips = 0
  do iterator = first, last, stride
    trips = trips + 1
    values(trips) = iterator
    if (trips >= cap) exit
  end do
  final_value = iterator
end subroutine

subroutine loop8_from64(first, last, stride, cap, values, trips, final_value)
  use iso_fortran_env, only: int8, int64
  implicit none
  integer(int64), intent(in) :: first, last, stride
  integer, intent(in) :: cap
  integer(int8), intent(out) :: values(4), final_value
  integer, intent(out) :: trips
  integer(int8) :: iterator
  trips = 0
  do iterator = first, last, stride
    trips = trips + 1
    values(trips) = iterator
    if (trips >= cap) exit
  end do
  final_value = iterator
end subroutine

subroutine loop8_from_real(first, last, stride, cap, values, trips, final_value)
  use iso_fortran_env, only: int8, real64
  implicit none
  real(real64), intent(in) :: first, last, stride
  integer, intent(in) :: cap
  integer(int8), intent(out) :: values(4), final_value
  integer, intent(out) :: trips
  integer(int8) :: iterator
  trips = 0
  do iterator = first, last, stride
    trips = trips + 1
    values(trips) = iterator
    if (trips >= cap) exit
  end do
  final_value = iterator
end subroutine

subroutine loop16(first, last, stride, cap, values, trips, final_value)
  use iso_fortran_env, only: int16
  implicit none
  integer(int16), intent(in) :: first, last, stride
  integer, intent(in) :: cap
  integer(int16), intent(out) :: values(4), final_value
  integer, intent(out) :: trips
  integer(int16) :: iterator
  trips = 0
  do iterator = first, last, stride
    trips = trips + 1
    values(trips) = iterator
    if (trips >= cap) exit
  end do
  final_value = iterator
end subroutine

subroutine loop32(first, last, stride, cap, values, trips, final_value)
  use iso_fortran_env, only: int32
  implicit none
  integer(int32), intent(in) :: first, last, stride
  integer, intent(in) :: cap
  integer(int32), intent(out) :: values(4), final_value
  integer, intent(out) :: trips
  integer(int32) :: iterator
  trips = 0
  do iterator = first, last, stride
    trips = trips + 1
    values(trips) = iterator
    if (trips >= cap) exit
  end do
  final_value = iterator
end subroutine

subroutine loop32_descending(first, last, cap, values, trips, final_value)
  use iso_fortran_env, only: int32
  implicit none
  integer(int32), intent(in) :: first, last
  integer, intent(in) :: cap
  integer(int32), intent(out) :: values(4), final_value
  integer, intent(out) :: trips
  integer(int32) :: iterator
  trips = 0
  do iterator = first, last, -1
    trips = trips + 1
    values(trips) = iterator
    if (trips >= cap) exit
  end do
  final_value = iterator
end subroutine

subroutine loop32_rounded_step(first, last, values, trips, final_value)
  use iso_fortran_env, only: int32
  implicit none
  integer(int32), intent(in) :: first, last
  integer(int32), intent(out) :: values(4), final_value
  integer, intent(out) :: trips
  integer(int32) :: iterator
  trips = 0
  do iterator = first, last, 3 - 2 * int(1.99999999)
    trips = trips + 1
    values(trips) = iterator
  end do
  final_value = iterator
end subroutine

subroutine loop64(first, last, stride, cap, values, trips, final_value)
  use iso_fortran_env, only: int64
  implicit none
  integer(int64), intent(in) :: first, last, stride
  integer, intent(in) :: cap
  integer(int64), intent(out) :: values(4), final_value
  integer, intent(out) :: trips
  integer(int64) :: iterator
  trips = 0
  do iterator = first, last, stride
    trips = trips + 1
    values(trips) = iterator
    if (trips >= cap) exit
  end do
  final_value = iterator
end subroutine

subroutine loop32_stride5(first, last, values, trips, final_value)
  use iso_fortran_env, only: int32
  implicit none
  integer(int32), intent(in) :: first, last
  integer(int32), intent(out) :: values(4), final_value
  integer, intent(out) :: trips
  integer(int32) :: iterator, f2c_do_safe_7 = 17, f2c_do_final_7 = 19
  trips = 0
  do iterator = first, last, 5
    trips = trips + 1
    values(trips) = iterator
  end do
  final_value = iterator
  if (f2c_do_safe_7 /= 17 .or. f2c_do_final_7 /= 19) stop 91
end subroutine

subroutine loop32_stride_minus5(first, last, values, trips, final_value)
  use iso_fortran_env, only: int32
  implicit none
  integer(int32), intent(in) :: first, last
  integer(int32), intent(out) :: values(4), final_value
  integer, intent(out) :: trips
  integer(int32) :: iterator
  trips = 0
  do iterator = first, last, -5
    trips = trips + 1
    values(trips) = iterator
  end do
  final_value = iterator
end subroutine
