module dtio_control_types
  implicit none
  type :: payload
    real(kind=8) :: value
  contains
    procedure :: output => write_payload
    procedure :: input => read_payload
    generic :: write(formatted) => output
    generic :: read(formatted) => input
  end type payload
contains
  subroutine write_payload(dtv, unit, iotype, v_list, iostat, iomsg)
    class(payload), intent(in) :: dtv
    integer, intent(in) :: unit, v_list(:)
    character(*), intent(in) :: iotype
    integer, intent(out) :: iostat
    character(*), intent(inout) :: iomsg
    character(24) :: decimal_mode, round_mode, sign_mode, delim_mode, expected_sign
    inquire(unit, decimal=decimal_mode, round=round_mode, sign=sign_mode, delim=delim_mode, &
            iostat=iostat, iomsg=iomsg)
    if (unit < 0) then
      if (iostat == 0) stop 15
      iostat = 0
    else
      if (iostat /= 0) return
      if (decimal_mode /= 'COMMA' .or. round_mode /= 'UP') stop 1
      if (iotype == 'NAMELIST' .and. delim_mode /= 'QUOTE') stop 2
    end if
    if (size(v_list) > 0) then
      if (v_list(1) >= 3) then
        if (v_list(1) == 3) then
          expected_sign = 'PROCESSOR_DEFINED'
        else if (v_list(1) == 4) then
          expected_sign = 'SUPPRESS'
        else
          expected_sign = 'PLUS'
        end if
        if (unit < 0 .or. sign_mode /= expected_sign) stop 22
        write(unit, '(F6.1)', iostat=iostat, iomsg=iomsg) dtv%value
        return
      end if
    end if
    if (unit >= 0) then
      if (sign_mode /= 'PLUS') stop 23
    end if
    write(unit, '(F6.1)', iostat=iostat, iomsg=iomsg) dtv%value
    if (iostat /= 0) return
    if (size(v_list) == 0) return
    if (v_list(1) == 2) then
      write(unit, '(/,F6.1)', decimal='point', round='down', sign='suppress', &
            iostat=iostat, iomsg=iomsg) dtv%value
    else
      write(unit, '(F6.1)', decimal='point', round='down', sign='suppress', &
            iostat=iostat, iomsg=iomsg) dtv%value
    end if
    if (iostat /= 0) return
    if (unit >= 0) then
      inquire(unit, decimal=decimal_mode, round=round_mode, sign=sign_mode, iostat=iostat)
      if (decimal_mode /= 'COMMA' .or. round_mode /= 'UP' .or. sign_mode /= 'PLUS') stop 3
    end if
    write(unit, '(F6.1)', iostat=iostat, iomsg=iomsg) dtv%value
  end subroutine write_payload

  subroutine read_payload(dtv, unit, iotype, v_list, iostat, iomsg)
    class(payload), intent(inout) :: dtv
    integer, intent(in) :: unit, v_list(:)
    character(*), intent(in) :: iotype
    integer, intent(out) :: iostat
    character(*), intent(inout) :: iomsg
    character(24) :: decimal_mode, round_mode, sign_mode
    inquire(unit, decimal=decimal_mode, round=round_mode, sign=sign_mode, iostat=iostat)
    if (unit < 0) then
      if (iostat == 0) stop 16
      iostat = 0
    else
      if (decimal_mode /= 'COMMA' .or. round_mode /= 'UP') stop 4
      if (sign_mode /= 'PLUS') stop 24
    end if
    if (size(v_list) /= 0 .or. len(iotype) == 0) stop 5
    if (iotype == 'NAMELIST' .or. iotype == 'LISTDIRECTED') then
      read(unit, *, iostat=iostat, iomsg=iomsg) dtv%value
    else
      read(unit, '(F6.1)', iostat=iostat, iomsg=iomsg) dtv%value
    end if
  end subroutine read_payload
end module dtio_control_types
