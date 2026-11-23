import { useState } from 'react';
import { Button, Field, Title } from '../ui';

export default function WelcomeScreen({ onSubmit }: { onSubmit: (name: string) => void }) {
  const [name, setName] = useState('');
  const valid = name.trim() !== '';

  function submit() {
    if (valid) onSubmit(name.trim());
  }

  return (
    <>
      <Title subtitle="What is your name?">Welcome</Title>
      <Field label="Name" value={name} onChangeText={setName} onSubmitEditing={submit} maxLength={30} />
      <Button label="Submit" onPress={submit} disabled={!valid} />
    </>
  );
}
