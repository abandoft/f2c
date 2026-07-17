subroutine many_arguments(x01, x02, x03, x04, x05, x06, x07, x08, x09, x10, &
                           x11, x12, x13, x14, x15, x16, x17, x18, x19, x20, &
                           x21, x22, x23, x24, x25, x26, x27, x28, x29, x30, &
                           x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, &
                           x41, x42, x43, x44, x45, x46, x47, x48, x49, x50, &
                           x51, x52, x53, x54, x55, x56, x57, x58, x59, x60, &
                           x61, x62, x63, x64, x65, x66, x67, x68, x69, x70)
  implicit none
  integer, intent(in) :: x01, x02, x03, x04, x05, x06, x07, x08, x09, x10
  integer, intent(in) :: x11, x12, x13, x14, x15, x16, x17, x18, x19, x20
  integer, intent(in) :: x21, x22, x23, x24, x25, x26, x27, x28, x29, x30
  integer, intent(in) :: x31, x32, x33, x34, x35, x36, x37, x38, x39, x40
  integer, intent(in) :: x41, x42, x43, x44, x45, x46, x47, x48, x49, x50
  integer, intent(in) :: x51, x52, x53, x54, x55, x56, x57, x58, x59, x60
  integer, intent(in) :: x61, x62, x63, x64, x65, x66, x67, x68, x69
  integer, intent(out) :: x70
  x70 = x01 + x10 + x20 + x30 + x40 + x50 + x60 + x69
end subroutine many_arguments

program many_arguments_test
  implicit none
  integer :: x01, x02, x03, x04, x05, x06, x07, x08, x09, x10
  integer :: x11, x12, x13, x14, x15, x16, x17, x18, x19, x20
  integer :: x21, x22, x23, x24, x25, x26, x27, x28, x29, x30
  integer :: x31, x32, x33, x34, x35, x36, x37, x38, x39, x40
  integer :: x41, x42, x43, x44, x45, x46, x47, x48, x49, x50
  integer :: x51, x52, x53, x54, x55, x56, x57, x58, x59, x60
  integer :: x61, x62, x63, x64, x65, x66, x67, x68, x69, x70
  x01 = 1
  x02 = 2
  x03 = 3
  x04 = 4
  x05 = 5
  x06 = 6
  x07 = 7
  x08 = 8
  x09 = 9
  x10 = 10
  x11 = 11
  x12 = 12
  x13 = 13
  x14 = 14
  x15 = 15
  x16 = 16
  x17 = 17
  x18 = 18
  x19 = 19
  x20 = 20
  x21 = 21
  x22 = 22
  x23 = 23
  x24 = 24
  x25 = 25
  x26 = 26
  x27 = 27
  x28 = 28
  x29 = 29
  x30 = 30
  x31 = 31
  x32 = 32
  x33 = 33
  x34 = 34
  x35 = 35
  x36 = 36
  x37 = 37
  x38 = 38
  x39 = 39
  x40 = 40
  x41 = 41
  x42 = 42
  x43 = 43
  x44 = 44
  x45 = 45
  x46 = 46
  x47 = 47
  x48 = 48
  x49 = 49
  x50 = 50
  x51 = 51
  x52 = 52
  x53 = 53
  x54 = 54
  x55 = 55
  x56 = 56
  x57 = 57
  x58 = 58
  x59 = 59
  x60 = 60
  x61 = 61
  x62 = 62
  x63 = 63
  x64 = 64
  x65 = 65
  x66 = 66
  x67 = 67
  x68 = 68
  x69 = 69
  x70 = 0
  call many_arguments(x01, x02, x03, x04, x05, x06, x07, x08, x09, x10, &
                      x11, x12, x13, x14, x15, x16, x17, x18, x19, x20, &
                      x21, x22, x23, x24, x25, x26, x27, x28, x29, x30, &
                      x31, x32, x33, x34, x35, x36, x37, x38, x39, x40, &
                      x41, x42, x43, x44, x45, x46, x47, x48, x49, x50, &
                      x51, x52, x53, x54, x55, x56, x57, x58, x59, x60, &
                      x61, x62, x63, x64, x65, x66, x67, x68, x69, x70)
  if (x70 /= 280) error stop
end program many_arguments_test
