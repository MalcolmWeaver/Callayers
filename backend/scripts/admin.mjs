// Uses the operator's application-default Google credentials, never a desktop session.
import { initializeApp, applicationDefault } from 'firebase-admin/app';
import { getAuth } from 'firebase-admin/auth';
import { getFirestore } from 'firebase-admin/firestore';
const [project, email, action] = process.argv.slice(2);
if (!project || !email || !['grant','revoke'].includes(action)) throw new Error('Usage: npm run admin -- PROJECT_ID EMAIL grant|revoke');
initializeApp({ credential: applicationDefault(), projectId: project });
const user = await getAuth().getUserByEmail(email);
if (!user.emailVerified || !user.providerData.some(p => p.providerId === 'google.com')) throw new Error('First sign in to the browser page with this Google account.');
const db = getFirestore(); const batch = db.batch(); const ref = db.collection('admins').doc(user.uid);
if (action === 'grant') batch.set(ref, { grantedAt: Date.now() }); else batch.delete(ref);
batch.create(db.collection('adminAudit').doc(), { actor: 'bootstrap-operator', uid: user.uid, action, at: Date.now() });
await batch.commit(); console.log(`Administrator ${action} completed for ${email}.`);
