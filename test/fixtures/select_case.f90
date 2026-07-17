program select_case_semantics
  implicit none
  integer :: hits
  integer :: value
  integer, parameter :: upper = 8
  character(len=4) :: text
  logical :: flag

  hits = 0
  do value = -2, 12
    select case (value)
    case (:-1)
      hits = hits + 1
    case (0, 2, 4)
      hits = hits + 10
    case (1, 3)
      hits = hits + 100
      select case (value)
      case (1)
        hits = hits + 0
      case (3)
        hits = hits + 0
      case default
        stop 8
      end select
    case (5:upper)
      hits = hits + 1000
    case (10:)
      hits = hits + 10000
    case default
      hits = hits + 100000
    end select
  end do

  text = "m"
  select case (text)
  case (:"f")
    stop 1
  case ("g":"r")
    hits = hits + 1
  case default
    stop 2
  end select

  text = "z"
  select case (text)
  case default
    hits = hits + 10
  case (:"f")
    stop 3
  case ("g":"r")
    stop 4
  end select

  text = "b"
  select case (text)
  case ("b")
    hits = hits + 10000
  case default
    stop 9
  end select

  flag = .true.
  select case (flag)
  case (.not. .false.)
    hits = hits + 100
  case (.false.)
    stop 5
  end select

  flag = .false.
  select case (flag)
  case (.true.)
    stop 6
  case (.false.)
    hits = hits + 1000
  end select

  if (hits /= 145343) stop 7
  write (*, '(I0)') hits
end program select_case_semantics
