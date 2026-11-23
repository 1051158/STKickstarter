import { useState } from 'react';
import { Room, supabase } from '../lib/supabase';
import { Button, ErrorText, Field, SegmentedControl, Title } from '../ui';

const modes = ['Join', 'Create'] as const;
type Props = { displayName: string; onOpened: (room: Room) => void };

export default function GroupsScreen({ displayName, onOpened }: Props) {
  const [mode, setMode] = useState<(typeof modes)[number]>('Join');
  const [name, setName] = useState('');
  const [pin, setPin] = useState('');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const join = mode === 'Join';
  const valid = name.trim() !== '' && (!join || /^\d{6}$/.test(pin));

  async function submit() {
    setLoading(true);
    setError(null);
    const { data, error } = join
      ? await supabase.rpc('join_room', { p_name: name.trim(), p_pin: pin, p_display_name: displayName })
      : await supabase.rpc('create_room', { p_name: name.trim(), p_display_name: displayName });
    setLoading(false);
    if (error) return setError(error.message);
    const room = data as Room | null;
    if (!room?.id) return setError('Group not found. Check the name and PIN.');
    onOpened(room);
  }

  return (
    <>
      <Title subtitle="Join your friends or start a new group">Groups</Title>
      <SegmentedControl
        options={modes}
        value={mode}
        onChange={(m) => {
          setMode(m);
          setError(null);
        }}
      />
      <Field label="Group name" value={name} onChangeText={setName} maxLength={40} />
      {join && (
        <Field
          label="PIN"
          value={pin}
          onChangeText={(text) => setPin(text.replace(/\D/g, ''))}
          keyboardType="number-pad"
          inputMode="numeric"
          maxLength={6}
        />
      )}
      <Button label={join ? 'Join group' : 'Create group'} onPress={submit} disabled={!valid || loading} />
      <ErrorText message={error} />
    </>
  );
}
