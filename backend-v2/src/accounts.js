export function accountStore(db, timestamp) {
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
  };
}
