export function accountStore(db, timestamp, expiry = date => date) {
  return {
    async getOrCreate(uid, today) {
      const ref = db.collection('accounts').doc(uid);
      return db.runTransaction(async tx => {
        const existing = await tx.get(ref);
        if (existing.exists) return existing.data();
        const account = { schemaVersion: 2, access: 'enabled',
          createdAt: timestamp(), usageDate: today, dailyGenerations: 0 };
        tx.create(ref, account);
        return account;
      });
    },
    async reserve(uid,id,today,limit,expiresAt) {
      const accountRef=db.collection('accounts').doc(uid),requestRef=db.collection('requests').doc(id);
      return db.runTransaction(async tx=>{
        const previous=await tx.get(requestRef);
        if(previous.exists){const record=previous.data();if(record.uid!==uid)return {kind:'wrong_owner'};return {kind:'duplicate',status:record.status};}
        const current=await tx.get(accountRef),account=current.data();
        if(!account || account.schemaVersion!==2 || account.access!=='enabled')return {kind:'denied'};
        if(!Number.isSafeInteger(account.dailyGenerations)||account.dailyGenerations<0)throw new Error('invalid_account');
        const used=account.usageDate===today?account.dailyGenerations:0;
        if(used>=limit)return {kind:'limit',used};
        tx.update(accountRef,{usageDate:today,dailyGenerations:used+1});
        tx.create(requestRef,{uid,status:'processing',createdAt:timestamp(),expiresAt:expiry(expiresAt)});
        return {kind:'reserved',used:used+1};
      });
    },
    async finish(uid,id,status) {
      const ref=db.collection('requests').doc(id);
      await db.runTransaction(async tx=>{const entry=await tx.get(ref);if(!entry.exists||entry.data().uid!==uid)throw new Error('invalid_request_owner');tx.update(ref,{status});});
    },
  };
}
