import { useState } from 'react';
import { Room, supabase } from '../lib/supabase';
import { Button, ErrorText, Field, Screen, Title } from '../ui';

type Props = { onBack: () => void; onCreated: (room: Room) => void };

export default function CreateRoomScreen({ onBack, onCreated }: Props) {
  const [name, setName] = useState('');
  const [displayName, setDisplayName] = useState('');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const valid = name.trim() !== '' && displayName.trim() !== '';

  async function create() {
    setLoading(true);
    setError(null);
    const { data, error } = await supabase.rpc('create_room', {
      p_name: name.trim(),
      p_display_name: displayName.trim(),
    });
    setLoading(false);
    if (error) return setError(error.message);
    onCreated(data as Room);
  }

  return (
    <Screen>
      <Button label="Back" onPress={onBack} />
      <Title>Create a room</Title>
      <Field label="Room name" value={name} onChangeText={setName} maxLength={40} />
      <Field label="Your name" value={displayName} onChangeText={setDisplayName} maxLength={30} />
      <Button label="Create" onPress={create} primary disabled={!valid || loading} />
      <ErrorText message={error} />
    </Screen>
  );
}
