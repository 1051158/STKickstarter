import { useState } from 'react';
import { Room, supabase } from '../lib/supabase';
import { Button, ErrorText, Field, Screen, Title } from '../ui';

type Props = { onBack: () => void; onJoined: (room: Room) => void };

export default function JoinRoomScreen({ onBack, onJoined }: Props) {
  const [name, setName] = useState('');
  const [pin, setPin] = useState('');
  const [displayName, setDisplayName] = useState('');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const valid = name.trim() !== '' && /^\d{6}$/.test(pin) && displayName.trim() !== '';

  async function join() {
    setLoading(true);
    setError(null);
    const { data, error } = await supabase.rpc('join_room', {
      p_name: name.trim(),
      p_pin: pin,
      p_display_name: displayName.trim(),
    });
    setLoading(false);
    if (error) return setError(error.message);
    const room = data as Room | null;
    if (!room?.id) return setError('Room not found. Check the name and PIN.');
    onJoined(room);
  }

  return (
    <Screen>
      <Button label="Back" onPress={onBack} />
      <Title>Join a room</Title>
      <Field label="Room name" value={name} onChangeText={setName} maxLength={40} />
      <Field
        label="PIN"
        value={pin}
        onChangeText={(text) => setPin(text.replace(/\D/g, ''))}
        keyboardType="number-pad"
        inputMode="numeric"
        maxLength={6}
      />
      <Field label="Your name" value={displayName} onChangeText={setDisplayName} maxLength={30} />
      <Button label="Join" onPress={join} primary disabled={!valid || loading} />
      <ErrorText message={error} />
    </Screen>
  );
}
