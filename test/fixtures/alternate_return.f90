module alternate_return_module
  implicit none
  abstract interface
    subroutine alternate_callback(mode, *, *)
      integer, intent(in) :: mode
    end subroutine alternate_callback
  end interface
contains
  subroutine module_route(mode, *, *)
    integer, intent(in) :: mode
    return mode
  end subroutine module_route

  subroutine invoke_callback(callback, observed)
    procedure(alternate_callback) :: callback
    integer, intent(out) :: observed

    call callback(2, *10, *20)
    observed = -1
    return
10 observed = 1
    return
20 observed = 2
  end subroutine invoke_callback
end module alternate_return_module

program alternate_return
  use alternate_return_module, only: invoke_callback, module_route
  implicit none
  integer :: callback_value, value

  value = 0
  call module_route(1, *80, *81)
  error stop 79
80 value = value + 1000000
  goto 82
81 error stop 81
82 continue

  call route(0, value, *100, *101)
  value = value + 1
  goto 102
100 error stop 100
101 error stop 101
102 continue

  call route(1, value, *110, *111)
  error stop 109
110 value = value + 10
  goto 112
111 error stop 111
112 continue

  call route(2, value, *120, *121)
  error stop 119
120 error stop 120
121 value = value + 100
  call route(3, value, *130, *131)
  value = value + 1000
  goto 132
130 error stop 130
131 error stop 131
132 continue

  call route(-1, value, *140, *141)
  value = value + 10000
  goto 142
140 error stop 140
141 error stop 141
142 continue

  block
    integer, allocatable :: scratch(:)
    allocate(scratch(4))
    scratch = value
    call route(1, scratch(1), *150, *151)
    error stop 149
  end block
150 value = value + 100000
  goto 152
151 error stop 151
152 continue

  call inner_route(2, *160, *161)
  error stop 159
160 error stop 160
161 value = value + 10000000
  goto 162
162 continue

  call invoke_callback(module_route, callback_value)
  value = value + callback_value * 100000000

  write(*, '(I0)') value
contains
  subroutine inner_route(mode, *, *)
    integer, intent(in) :: mode
    return mode
  end subroutine inner_route
end program alternate_return

subroutine route(mode, value, *, *)
  implicit none
  integer, intent(in) :: mode
  integer, intent(inout) :: value

  value = value + 1
  select case (mode)
  case (1)
    return 1
  case (2)
    return 2
  case (3)
    return 3
  case (-1)
    return -1
  end select
  return
end subroutine route
