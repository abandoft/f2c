module live_character_result_model
  implicit none
contains
  function retarget(text, target) result(value)
    character(:), pointer, volatile, intent(inout) :: text
    character(*), target, intent(inout) :: target
    character(len=len(text)) :: value
    ! The result specification belongs to this invocation. Updating pointer
    ! association must not update that specification or restore the old state.
    if (len(value) /= 2) stop 1
    text => target
    if (len(value) /= 2 .or. len(text) /= 5) stop 2
    value = 'ok'
  end function retarget
end module live_character_result_model

program live_character_result_state
  use live_character_result_model
  implicit none
  character(2), target :: original = 'ab'
  character(5), target :: replacement = 'live!'
  character(:), pointer, volatile :: text
  character(2) :: value
  text => original
  value = retarget(text,replacement)
  if (value /= 'ok') stop 3
  if (len(text) /= 5 .or. text /= 'live!') stop 4
  nullify(text)
  print '(a)', 'live character result state contracts passed'
end program live_character_result_state
